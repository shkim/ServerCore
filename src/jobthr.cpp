#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

static ATOMIC32 s_nThreadIndexCounter =0;
static ATOMIC32 s_nJobThreadCreated = 0;

#ifdef SC_NO_AUTOTLS

TLSID_TYPE tlsId_ThreadIndex;

void SetMyThreadIndex()
{
	ATOMIC32 i = _InterlockedIncrement(&s_nThreadIndexCounter);
	TlsSetValue(tlsId_ThreadIndex, (void*) i);
}

int _GetThreadIndex()
{
	return (int) (size_t) TlsGetValue(tlsId_ThreadIndex);
}

#else

SC_DECLSPEC_THREAD ATOMIC32 tls_i = 0;

void SetMyThreadIndex()
{
	tls_i = _InterlockedIncrement(&s_nThreadIndexCounter);
}

int _GetThreadIndex()
{
	return tls_i;
}

#endif

//////////////////////////////////////////////////////////////////////////////
// Hazard Pointer impl

static HazardPtrT* s_pHazardPtrList;	// shared hazard ptrs
static LocklessStack s_hptPool;	// pool for HazardPtrT

struct HazardRetireItem
{
	HazardRetireItem* pNext;
	void* ptrToFree;
	HazardReclaimFuncT pfnReclaim;
};

static HazardRetireItem* s_pHazardRemainList = NULL;
static LocklessStack s_rtiPool;	// pool for HazardRetireItem


#ifdef SC_NO_AUTOTLS

TLSID_TYPE tlsId_HazardRetireList;
TLSID_TYPE tlsId_HazardRetireListSize;

HazardRetireItem* tls_GetHazardRetireList()
{
	return (HazardRetireItem*) TlsGetValue(tlsId_HazardRetireList);
}

int tls_GetHazardRetireListSize()
{
	return (int) (size_t) TlsGetValue(tlsId_HazardRetireListSize);
}

void tls_SetHazardRetireList(HazardRetireItem* p)
{
	TlsSetValue(tlsId_HazardRetireList, p);
}

void tls_SetHazardRetireListSize(int n)
{
	TlsSetValue(tlsId_HazardRetireListSize, (void*) n);
}

#else

// thread-local retire list
SC_DECLSPEC_THREAD HazardRetireItem* tls_pHazardRetireList = NULL;
SC_DECLSPEC_THREAD int tls_nHazardRetireListSize = 0;

#define tls_GetHazardRetireList()			(tls_pHazardRetireList)
#define tls_GetHazardRetireListSize()		(tls_nHazardRetireListSize)

#define tls_SetHazardRetireList(_p)			(tls_pHazardRetireList = _p)
#define tls_SetHazardRetireListSize(_n)		(tls_nHazardRetireListSize = _n)

#endif

HazardPtrT* _HazardAcquire(void** pp)
{
	HazardPtrT* pRet = s_pHazardPtrList;
	for(; pRet != NULL; pRet = pRet->__notouch_HazardPtrNext)
	{
		if(pRet->__notouch_PtrIsActive)
			continue;

		if(_InterlockedCompareExchange(&pRet->__notouch_PtrIsActive, 1, 0) == 0)
		{
_setPtr:
			if(pp == NULL)
			{
				pRet->ptr = NULL;
			}
			else
			{
				void* ptr;
				do {
					ptr = *pp;
					pRet->ptr = ptr;
				} while(*pp != ptr);
			}

			return pRet;
		}
	}

	for(;;)
	{
		pRet = (HazardPtrT*) s_hptPool.Pop();

		if(pRet != NULL)
		{
			pRet->__notouch_PtrIsActive = 1;
			pRet->ptr = NULL;

			HazardPtrT* pOld;
			do {
				pOld = s_pHazardPtrList;
				pRet->__notouch_HazardPtrNext = pOld;
			} while(_InterlockedCompareExchangePointer(
				(void**)&s_pHazardPtrList, pRet, pOld) != pOld);

			goto _setPtr;
		}

		int nAlloc = 1024 * 4;
		int cbItem = sizeof(HazardPtrT);
		BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
		while(nAlloc-- > 0)
		{
			s_hptPool.Push((LocklessEntryT*)pFree);
			pFree += cbItem;
		}
	}
}

void _HazardRelease(HazardPtrT* p)
{
	p->ptr = NULL;
	p->__notouch_PtrIsActive = 0;	
}

static void HazardGarbageCollect()
{
	SC_ASSERT(tls_GetHazardRetireListSize() > 0);

	std::vector<void*> hp;

	HazardPtrT* pHead = s_pHazardPtrList;
	while(pHead)
	{
		void* p = pHead->ptr;
		if(p) hp.push_back(p);
		pHead = pHead->__notouch_HazardPtrNext;
	}

	std::sort(hp.begin(), hp.end(), std::less<void*>());

	HazardRetireItem* pLiveList = NULL;
	HazardRetireItem* pRetire = tls_GetHazardRetireList();
	while(pRetire)
	{
		HazardRetireItem* pNext = pRetire->pNext;

		if(std::binary_search(hp.begin(), hp.end(), pRetire->ptrToFree))
		{
			pRetire->pNext = pLiveList;
			pLiveList = pRetire;
		}
		else
		{
			pRetire->pfnReclaim(pRetire->ptrToFree);
			s_rtiPool.Push((LocklessEntryT*)pRetire);
#ifdef SC_NO_AUTOTLS
			tls_SetHazardRetireListSize(tls_GetHazardRetireListSize() -1);
#else
			--tls_nHazardRetireListSize;
#endif
		}

		pRetire = pNext;
	}

	tls_SetHazardRetireList(pLiveList);
}

void _HazardRetire(void* ptrToFree, HazardReclaimFuncT pfnReclaim)
{
	HazardRetireItem* pItem;
	for(;;)
	{
		pItem = (HazardRetireItem*) s_rtiPool.Pop();
		if(pItem != NULL)
		{
			pItem->ptrToFree = ptrToFree;
			pItem->pfnReclaim = pfnReclaim;
			pItem->pNext = tls_GetHazardRetireList();
			tls_SetHazardRetireList(pItem);
			break;
		}

		int nAlloc = 1024 * 4;
		int cbItem = sizeof(HazardRetireItem);
		BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
		while(nAlloc-- > 0)
		{
			s_rtiPool.Push((LocklessEntryT*)pFree);
			pFree += cbItem;
		}
	}

#ifdef SC_NO_AUTOTLS
	int size = tls_GetHazardRetireListSize() +1;
	tls_SetHazardRetireListSize(size);
#else
	int size = ++tls_nHazardRetireListSize;
#endif

	if(size > 4)
	{
		HazardGarbageCollect();
	}
}

void Hazard_OnThreadExit()
{
	if(tls_GetHazardRetireList() != NULL)
	{
		HazardGarbageCollect();

		HazardRetireItem* pItem = tls_GetHazardRetireList();
		while(pItem)
		{
			HazardRetireItem* _pNext = pItem->pNext;

			HazardRetireItem* pOld;
			do {
				pOld = s_pHazardRemainList;
				pItem->pNext = pOld;
			} while(_InterlockedCompareExchangePointer(
				(void**)&s_pHazardRemainList, pItem, pOld) != pOld);

			pItem = _pNext;
		}
	}
}

void Hazard_OnFinalReclaimAll()
{
	SC_ASSERT(ScGetThreadIndex() == 0);

	while(s_pHazardRemainList)
	{
		s_pHazardRemainList->pfnReclaim(s_pHazardRemainList->ptrToFree);
		s_pHazardRemainList = s_pHazardRemainList->pNext;
	}
}

//////////////////////////////////////////////////////////////////////////////

BYTE* GrowOnlyAllocList::Alloc(int* pAllocSizeIn_ItemCntOut, int* pItemSizeAdjust)
{
	int cbAlloc = *pAllocSizeIn_ItemCntOut;
	AllocBlock* pBlock = (AllocBlock*) ScAlignedAlloc(cbAlloc, MEMORY_ALLOCATION_ALIGNMENT);
	SC_ASSERT(pBlock != NULL);

	do {
		pBlock->pNext = m_pAllocList;
	} while(_InterlockedCompareExchangePointer((void**)&m_pAllocList,
		pBlock, pBlock->pNext) != pBlock->pNext);

	BYTE* pFree = (BYTE*)pBlock + MEMORY_ALLOCATION_ALIGNMENT;
	*pItemSizeAdjust = (*pItemSizeAdjust + (MEMORY_ALLOCATION_ALIGNMENT-1)) & ~(MEMORY_ALLOCATION_ALIGNMENT-1);
	*pAllocSizeIn_ItemCntOut = (cbAlloc - MEMORY_ALLOCATION_ALIGNMENT) / *pItemSizeAdjust;

#ifdef _DEBUG
	Log(LOG_DEBUG, "Alloc %d bytes for GPool ([%d]x%d items)\n",
		cbAlloc, *pItemSizeAdjust, *pAllocSizeIn_ItemCntOut);
#endif

	return pFree;
}

void GrowOnlyAllocList::FreeAll()
{
	while(m_pAllocList != NULL)
	{
		AllocBlock* pNext = m_pAllocList->pNext;
		ScAlignedFree(m_pAllocList);
		m_pAllocList = pNext;
	}
}

//////////////////////////////////////////////////////////////////////////////

JobThread::JobThread(JobQueue* pAssocQueue, IJobThreadListener* pLsnr)
{
	m_pAssocQueue = pAssocQueue;
	m_pListener = pLsnr;
	m_pMoreWorkerNext = NULL;
	_GetPthreadHandle(m_hThread) = INVALID_THREAD_HANDLE_VALUE;
}

JobThread::~JobThread()
{
	SC_ASSERT(!IsThreadCreated());
}

void JobThread::ThreadProc()
{
	JobQueue::JobItem item;

	{
		bool ret = m_pListener->OnCreate();
		_InterlockedIncrement(&s_nJobThreadCreated);

		if(!ret)
		{
			g_core.Shutdown();
			return;
		}
	}

	for(;;)
	{
		m_pAssocQueue->Dequeue(&item);

		if(!g_bServerRunning)
		{
			Log(LOG_SYSTEM, "Quit from JobThread.\n");
			break;
		}

		m_pListener->OnDispatch(item.nJobID, item.nParam, item.pParam);

		if(item.pSender != NULL)
			item.pSender->Ref_Release();
	}

	m_pListener->OnDestroy();

	_InterlockedDecrement(&s_nJobThreadCreated);
}

static
#ifndef SC_PLATFORM_POSIX
unsigned int __stdcall
#else
void*
#endif
_JobThreadProc(void *pv)
{
	SetMyThreadIndex();

	((JobThread*) pv)->ThreadProc();

	Hazard_OnThreadExit();

#ifdef SC_PLATFORM_POSIX
	pthread_exit(NULL);
#endif

	return 0;
}

bool JobThread::Spawn()
{
#ifndef SC_PLATFORM_POSIX
	SC_ASSERT(m_hThread == INVALID_THREAD_HANDLE_VALUE);
	m_hThread = (HANDLE) _beginthreadex(NULL, 0, _JobThreadProc, this, 0, &m_dwThreadID);
	if(m_hThread == INVALID_THREAD_HANDLE_VALUE)
#else
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	int err = pthread_create(&m_hThread, &attr, _JobThreadProc, this);
	pthread_attr_destroy(&attr);
	if(err != 0)
#endif
	{
		Log(LOG_FATAL, "Job thread spawn failed.\n");
		return false;
	}

	SC_ASSERT(IsThreadCreated());
	return true;
}

void JobThread::Terminate()
{
	if(IsThreadCreated())
	{
#ifndef SC_PLATFORM_POSIX
		WaitForSingleObject(m_hThread, 5000);
		CloseHandle(m_hThread);
#else
		pthread_join(m_hThread, NULL);
#endif
		_GetPthreadHandle(m_hThread) = INVALID_THREAD_HANDLE_VALUE;
	}
}

//////////////////////////////////////////////////////////////////////////////

LocklessStack JobQueue::s_freeNodePool;

JobQueue::JobQueue()
{
#ifdef SC_USE_IOCP_QUEUE
	m_hIocpQueue = NULL;
#endif
	m_pWorkers = NULL;
}

bool JobQueue::Create(JobThread* pThread)
{
	SC_ASSERT(m_pWorkers == NULL);
	m_pWorkers = pThread;

#ifdef SC_USE_IOCP_QUEUE
	m_hIocpQueue = CreateNewCompletionPort(0);
	m_nItemCount = 0;
#else
	m_nWaitCount = 1;
	m_queue.Create(this);
	m_condivar.Create();
	m_cs.Create();
#endif

//	Log(LOG_DATE|LOG_SYSTEM, "JobQueue created.\n");
	return true;
}

void JobQueue::Destroy()
{
	JobThread* pNextThr;
	for(JobThread* pThr = m_pWorkers; pThr != NULL; pThr = pNextThr)
	{
		pNextThr = pThr->m_pMoreWorkerNext;
		delete pThr;
	}

#ifdef SC_USE_IOCP_QUEUE
	CloseHandle(m_hIocpQueue);
#else
	// FIXME: not-processed job items leaked
	JobItem leakItem;
	while(m_queue.Pop(&leakItem));

	m_queue.Destroy();
	m_cs.Destroy();
	m_condivar.Destroy();
#endif
}

LocklessEntryT* JobQueue::GetQueueNodeItem()
{
	QNodeItem* pItem = (QNodeItem*) s_freeNodePool.Pop();
	while(pItem == NULL)
	{
		int nAlloc = 1024 * 4;
		int cbItem = sizeof(QNodeItem);
		BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
		while(nAlloc-- > 0)
		{
			s_freeNodePool.Push((LocklessEntryT*)pFree);
			pFree += cbItem;
		}

		pItem = (QNodeItem*) s_freeNodePool.Pop();
	}

	return pItem;
}

void JobQueue::RemoveQueueNodeItem(LocklessEntryT* pItemToFree)
{
	s_freeNodePool.Push(pItemToFree);
}

void JobQueue::CopyQueueNodeData(const LocklessEntryT* pItem, void* param)
{
	memcpy(param, &((QNodeItem*)pItem)->data, sizeof(JobItem));
}

void JobQueue::Dequeue(JobItem* pOut)
{
#ifdef SC_USE_IOCP_QUEUE

	DWORD cbTransferred;
	OVERLAPPED* pOV;
	QNodeItem* pItem;

	GetQueuedCompletionStatus(m_hIocpQueue, &cbTransferred,
		(ULONG_PTR*) &pItem, &pOV, INFINITE);
	SC_ASSERT(cbTransferred == sizeof(QNodeItem) && pOV == NULL);

	*pOut = pItem->data;
	s_freeNodePool.Push(pItem);
	_InterlockedDecrement(&m_nItemCount);

#else

	if(m_queue.Pop(pOut))
		return;

	m_cs.Lock();
	while(!m_queue.Pop(pOut))
	{
		_InterlockedIncrement(&m_nWaitCount);
		m_condivar.Wait(&m_cs);
		_InterlockedDecrement(&m_nWaitCount);
	}
	m_cs.Unlock();

#endif
}

void JobQueue::Enqueue(QNodeItem* pNewNode)
{
#ifdef SC_USE_IOCP_QUEUE

	PostQueuedCompletionStatus(m_hIocpQueue, sizeof(QNodeItem), (ULONG_PTR) pNewNode, 0);
	_InterlockedIncrement(&m_nItemCount);

#else
	
	m_queue.Push(pNewNode);

	ATOMIC32 cnt = _InterlockedDecrement(&m_nWaitCount);
	_InterlockedIncrement(&m_nWaitCount);

	if(cnt >= 1)
		m_condivar.Wake();

#endif
}

void JobQueue::Post(IRefCounted* pSender, int nJobID, int nParam, void* pParam)
{
	if(pSender != NULL)
		pSender->Ref_Retain();

	QNodeItem* pNewNode = (QNodeItem*) GetQueueNodeItem();
	pNewNode->data.pSender = pSender;
	pNewNode->data.nJobID = nJobID;
	pNewNode->data.nParam = nParam;
	pNewNode->data.pParam = pParam;

	Enqueue(pNewNode);
}

void JobQueue::EnqueueQuitItem()
{
	SC_ASSERT(g_bServerRunning == false);
	QNodeItem* pNewNode = (QNodeItem*) GetQueueNodeItem();
	Enqueue(pNewNode);
}

int JobQueue::GetPendingJobCount()
{
#ifdef SC_USE_IOCP_QUEUE
	return m_nItemCount;
#else
	return m_queue.GetSize();
#endif
}

void JobQueue::AddMoreThread(IJobThreadListener* pJTL)
{
	SC_ASSERT(m_pWorkers != NULL);

	if(g_bServerRunning)
	{
		Log(LOG_FATAL, "AddMoreThread ignored. (not callable state)\n");
		return;
	}

	JobThread* pMoreWorker = new JobThread(this, pJTL);
	if(pMoreWorker == NULL)
	{
		Log(LOG_FATAL, "AddMoreThread: JobThread alloc failed.\n");
		return;
	}

	pMoreWorker->m_pMoreWorkerNext = m_pWorkers;
	m_pWorkers = pMoreWorker;
}

//////////////////////////////////////////////////////////////////////////////

IJobQueue* ServerCore::CreateJobThread(IJobThreadListener* pJTL)
{
	JobQueue* pJobQueue = new JobQueue();
	if(pJobQueue == NULL)
	{
		Log(LOG_FATAL, "CreateJobThread: JobQueue alloc failed.\n");
		return NULL;
	}

	JobThread* pJobThread = new JobThread(pJobQueue, pJTL);
	if(pJobThread == NULL)
	{
		delete pJobQueue;
		Log(LOG_FATAL, "CreateJobThread: JobThread alloc failed.\n");
		return NULL;
	}

	if(!pJobQueue->Create(pJobThread))
	{
		delete pJobThread;
		delete pJobQueue;
		Log(LOG_FATAL, "JobThread.Create failed.\n");
		return NULL;
	}

	m_CoreLock.Lock();
	{
		m_nJobThreadCount++;
		m_nJobQueueCount++;
		pJobQueue->x_pGlobalChainNext = m_pJobQueues;
		m_pJobQueues = pJobQueue;
	}
	m_CoreLock.Unlock();

	return pJobQueue;
}

// Actually create the os-thread for the previously-created JobThread instances.
// Used once on server startup by main thread
bool ServerCore::SpawnJobThreads()
{
	if(m_pJobQueues == NULL)
		return true;

#ifdef _DEBUG
	int cnt = m_nJobThreadCount;
#endif
	for(JobQueue* pJQ = m_pJobQueues; pJQ != NULL; pJQ = pJQ->x_pGlobalChainNext)
	{
		for(JobThread* pThr = pJQ->m_pWorkers; pThr != NULL; pThr = pThr->m_pMoreWorkerNext)
		{
			SC_ASSERT(pThr->IsThreadCreated() == false && cnt-- > 0);

			if(!pThr->Spawn())
				return false;
		}
	}

	SC_ASSERT(cnt == 0);
	Log(LOG_SYSTEM, "Created %d job queue(s), %d thread(s)\n", m_nJobQueueCount, m_nJobThreadCount);

	// wait job threads finished OnCreate call
	while(s_nJobThreadCreated < m_nJobThreadCount)
	{
		Sleep(100);
	}

	return true;
}

void ServerCore::TerminateJobThreads()
{
	if(m_nJobThreadCount <= 0)
		return;

	Log(LOG_SYSTEM, "Terminates %d job thread(s)\n", m_nJobThreadCount);

	for(JobQueue* pJQ = m_pJobQueues; pJQ != NULL; pJQ = pJQ->x_pGlobalChainNext)
	{
		JobThread* pThr = pJQ->m_pWorkers;
		for(; pThr != NULL; pThr = pThr->m_pMoreWorkerNext)
		{
			pJQ->EnqueueQuitItem();
		}

		pThr = pJQ->m_pWorkers;
		for(; pThr != NULL; pThr = pThr->m_pMoreWorkerNext)
		{
			pThr->Terminate();
		}
	}
}

void ServerCore::DestroyJobQueues()
{
	JobQueue* pNextQ;

	for(JobQueue* pJQ = m_pJobQueues; pJQ != NULL; pJQ = pNextQ)
	{
		pNextQ = pJQ->x_pGlobalChainNext;
		pJQ->Destroy();
		delete pJQ;
	}
}

SC_NAMESPACE_END
