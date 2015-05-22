#pragma once

SC_NAMESPACE_BEGIN

#if defined(_WIN32) && !defined(SC_PLATFORM_POSIX) && (_WIN32_WINNT < 0x0600)
#define SC_USE_IOCP_QUEUE
#endif

#ifdef SC_PLATFORM_POSIX
	#ifdef _WIN32
		#define _GetPthreadHandle(_pth)		(_pth.p)
		#define TLSID_TYPE DWORD
	#else
		#define _GetPthreadHandle(_pth)		(_pth)
		#define TLSID_TYPE pthread_key_t
		#define TlsSetValue pthread_setspecific
		#define TlsGetValue pthread_getspecific
	#endif
#else
	#define _GetPthreadHandle(_pth)		(_pth)
	#define TLSID_TYPE DWORD
#endif

#if defined(_WIN32) && !defined(SC_PLATFORM_POSIX)
#	define THREAD_HANDLE HANDLE
#else
#	define THREAD_HANDLE pthread_t
#endif
#define INVALID_THREAD_HANDLE_VALUE	0

class JobQueue;

class JobThread
{
public:
	JobThread(JobQueue* pAssocQueue, IJobThreadListener* pLsnr);
	~JobThread();
	
	bool Spawn();
	void Terminate();
	void ThreadProc();

	inline bool IsThreadCreated() const { return (_GetPthreadHandle(m_hThread) != INVALID_THREAD_HANDLE_VALUE); }
//	inline JobQueue* GetQueue() { return m_pAssocQueue; }

	// don't touch these variables in JobThread member
	JobThread* m_pMoreWorkerNext;	// used by JobQueue
//	JobThread* x_pGlobalChainNext;	// used by SvrCoreImpl

protected:
	IJobThreadListener* m_pListener;
	JobQueue* m_pAssocQueue;

	THREAD_HANDLE m_hThread;
#ifndef SC_PLATFORM_POSIX
	UINT m_dwThreadID;
#endif
};

class JobQueue : public IJobQueue, public ILocklessQueueListener
{
public:
	JobQueue();

	bool Create(JobThread* pThread);
	void Destroy();

	virtual void Post(IRefCounted* pSender, int nJobID, int nParam, void* pParam);
	virtual int GetPendingJobCount();
	virtual void AddMoreThread(IJobThreadListener* pJTL);

	struct JobItem
	{
		IRefCounted* pSender;
		int nJobID;
		int nParam;
		void* pParam;
	};

	void Dequeue(JobItem* pOut);
	void EnqueueQuitItem();		// called on shutdown process

	static void DestroySharedPool();

	JobQueue* x_pGlobalChainNext;	// used by SvrCoreImpl; don't touch in this class
	JobThread* m_pWorkers;

private:
	static LocklessStack s_freeNodePool;

	struct QNodeItem : public LocklessEntryT
	{
		JobItem data;
	};

	// ILocklessQueueListener impl:
	virtual LocklessEntryT* GetQueueNodeItem();
	virtual void RemoveQueueNodeItem(LocklessEntryT* pItemToFree);
	virtual void CopyQueueNodeData(const LocklessEntryT* pItem, void* param);

	void Enqueue(QNodeItem* pNewNode);

#ifdef SC_USE_IOCP_QUEUE
	HANDLE m_hIocpQueue;
	ATOMIC32 m_nItemCount;
#else
	ATOMIC32 m_nWaitCount;
	LocklessQueue m_queue;
	CriticalSection m_cs;
	ConditionalVariable m_condivar;
#endif
};

class GrowOnlyAllocList
{
public:
	GrowOnlyAllocList() { m_pAllocList = NULL; }

	BYTE* Alloc(int* pAllocSizeIn_ItemCntOut, int* pItemSizeAdjust);
	void FreeAll();

private:
	struct AllocBlock
	{
		AllocBlock* pNext;
	};

	AllocBlock* m_pAllocList;
};

SC_NAMESPACE_END
