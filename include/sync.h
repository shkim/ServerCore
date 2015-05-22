#pragma once

SC_NAMESPACE_BEGIN

#ifdef _WIN32

class SpinLock
{
	volatile ATOMIC32 m_nThreadId;
	int m_nLockCount;

	enum
	{
		SPIN_COUNT = 0x1000,
		SLEEP_TIME = 0
	};

public:
	inline SpinLock() : m_nThreadId(0), m_nLockCount(0)
	{
	}

	inline void Lock()
	{
		ATOMIC32 curThreadId = (ATOMIC32) GetCurrentThreadId();
		if (curThreadId != m_nThreadId)
		{
			int nSpinCount = 0;
			while (_InterlockedCompareExchange(&m_nThreadId, curThreadId, 0))
			{
				if (SPIN_COUNT <= ++nSpinCount)
				{
					nSpinCount = 0;
					Sleep(SLEEP_TIME);
				}
			}
		}

		++m_nLockCount;
	}

	inline void Unlock()
	{
		SC_ASSERT((ATOMIC32) GetCurrentThreadId() == m_nThreadId);
		SC_ASSERT(m_nLockCount > 0);

		if (0 == --m_nLockCount)
		{
			m_nThreadId = 0;
		}
	}

	inline bool TryLock()
	{
		long curThreadId = (ATOMIC32) GetCurrentThreadId();
		if (curThreadId != m_nThreadId)
		{
			if (0 == _InterlockedCompareExchange(&m_nThreadId, curThreadId, 0))
			{
				return false;
			}	
		}

		++m_nLockCount;
		return true;
	}
};

class CriticalSection
{
	CRITICAL_SECTION m_cs;

public:
	inline void Create()
	{
		InitializeCriticalSection(&m_cs);
	}

	inline void Destroy()
	{
		DeleteCriticalSection(&m_cs);
	}

	inline void Lock()
	{
		EnterCriticalSection(&m_cs);
	}

	inline void Unlock()
	{
		LeaveCriticalSection(&m_cs);
	}
	
#if _WIN32_WINNT >= 0x0400
	inline BOOL TryLock()
	{
		return TryEnterCriticalSection(&m_cs);
	}
#endif

	friend class ConditionalVariable;
};

#if _WIN32_WINNT >= 0x0600

class RwLock
{
	SRWLOCK m_rwl;

public:
	inline bool Create()
	{
		InitializeSRWLock(&m_rwl);
		return true;
	}

	inline void Destroy()
	{
	}

	inline void LockRead()
	{
		AcquireSRWLockShared(&m_rwl);
	}

	inline void UnlockRead()
	{
		ReleaseSRWLockShared(&m_rwl);
	}

	inline void LockWrite()
	{
		AcquireSRWLockExclusive(&m_rwl);
	}

	inline void UnlockWrite()
	{
		ReleaseSRWLockExclusive(&m_rwl);
	}

	// unsupported but rather wait forever
	// (simple advice: do not use Try~ method)
	inline bool TryLockRead()
	{
		SC_ASSERT(!"TryLockRead unsupported");
		AcquireSRWLockShared(&m_rwl);
		return true;
	}

	inline bool TryLockWrite()
	{
		SC_ASSERT(!"TryLockWrite unsupported");
		AcquireSRWLockExclusive(&m_rwl);
		return true;
	}

	friend class ConditionalVariable;
};

class ConditionalVariable
{
	CONDITION_VARIABLE m_cond;

public:
	inline bool Create()
	{
		InitializeConditionVariable(&m_cond);
		return true;
	}

	inline void Destroy()
	{
	}

	inline void Wake()
	{
		WakeConditionVariable(&m_cond);
	}

	inline void WakeAll()
	{
		WakeAllConditionVariable(&m_cond);
	}

	// CriticalSection or RwLock must be in Locked state:

	inline BOOL Wait(CriticalSection* pCS)
	{
		return SleepConditionVariableCS(&m_cond, &pCS->m_cs, INFINITE);
	}

	inline BOOL Wait(CriticalSection* pCS, DWORD msTimeout)
	{
		return SleepConditionVariableCS(&m_cond, &pCS->m_cs, msTimeout);
	}

	inline BOOL Wait(RwLock* pRWL, bool isReadLock)
	{
		return SleepConditionVariableSRW(&m_cond, &pRWL->m_rwl, INFINITE,
			isReadLock ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0);
	}

	inline BOOL Wait(RwLock* pRWL, bool isReadLock, DWORD msTimeout)
	{
		return SleepConditionVariableSRW(&m_cond, &pRWL->m_rwl, msTimeout,
			isReadLock ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0);
	}
};

#else

class RwLock
{
	HANDLE m_hEvent;	// reader
	HANDLE m_hMutex;	// writer
	LONG m_nReaders;
	
	bool _ReadLock(int timeout)
	{
		DWORD ret = WaitForSingleObject(m_hMutex, timeout);
	
		if(ret != WAIT_OBJECT_0)
			return false;

		_InterlockedIncrement(&m_nReaders);

#ifdef _DEBUG
		if(!ResetEvent(m_hEvent))
		{
			Log(LOG_FATAL, "RwLock::_ReadLock - ResetEvent failed (err=%d)\n", GetLastError());
			return false;
		}

		if(!ReleaseMutex(m_hMutex))
		{
			Log(LOG_FATAL, "RwLock::_ReadLock - ReleaseMutex failed (err=%d)\n", GetLastError());
			return false;
		}
#else
		ResetEvent(m_hEvent);
		ReleaseMutex(m_hMutex);
#endif
		return true;
	}
	
	bool _WriteLock(int timeout)
	{
		DWORD ret = WaitForSingleObject(m_hMutex, timeout);

		if(ret != WAIT_OBJECT_0)
			return false;

		if(m_nReaders)
		{
			if(timeout == 0)
			{
				ReleaseMutex(m_hMutex);
				return false;
			}

#ifdef _DEBUG
			ret = WaitForSingleObject(m_hEvent, INFINITE);
			if(ret != WAIT_OBJECT_0)
			{
				Log(LOG_FATAL, "RwLock::_WriteLock - WaitEvent failed (err=%d)\n", GetLastError());
				ReleaseMutex(m_hMutex);
				return false;
			}
#else
			WaitForSingleObject(m_hEvent, INFINITE);
#endif
		}

		return true;
	}
	
public:
	RwLock()
	{
		m_hEvent = NULL;
		m_hMutex = NULL;
	}
	
	bool Create()
	{
		SC_ASSERT(m_hEvent == NULL && m_hMutex == NULL);
		m_nReaders = 0;
		
		m_hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);	// initially unsignaled manual-reset event
		if(m_hEvent == NULL)
		{
			return false;
		}
		
		m_hMutex = CreateMutex(NULL, FALSE, NULL);	// not owned unnamed mutex
		if(m_hMutex == NULL)
		{
			Destroy();
			return false;
		}
		
		return true;		
	}
	
	void Destroy()
	{
		if(m_hMutex)
		{
			CloseHandle(m_hMutex);
			m_hMutex = NULL;
		}
		
		if(m_hEvent)
		{
			CloseHandle(m_hEvent);
			m_hEvent = NULL;
		}
	}

	__forceinline void LockRead() { _ReadLock(INFINITE); }	
	__forceinline bool TryLockRead() {	return _ReadLock(0); }
	
	void UnlockRead()
	{
		SC_ASSERT(m_nReaders > 0);
		
		if(!_InterlockedDecrement(&m_nReaders)) 
			SetEvent(m_hEvent);
	}
	
	__forceinline void LockWrite() { _WriteLock(INFINITE);	}	
	__forceinline bool TryLockWrite() { return _WriteLock(0); }

	void UnlockWrite()
	{
#ifdef _DEBUG
		if(!ReleaseMutex(m_hMutex))
		{
			Log(LOG_FATAL,"RwLock::UnlockWrite - ReleaseMutex failed.\n");
		}
#else		
		ReleaseMutex(m_hMutex);
#endif
	}

	friend class ConditionalVariable;
};

// for source-level compatibility purpose, use Vista or later :)
class ConditionalVariable
{
	HANDLE m_hEvent;

public:
	inline ConditionalVariable()
	{
		m_hEvent = NULL;
	}

#ifdef _DEBUG
	inline ~ConditionalVariable() { SC_ASSERT(m_hEvent == NULL); }
#endif

	inline bool Create()
	{
		m_hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		return (m_hEvent != NULL);
	}

	inline void Destroy()
	{
		if(m_hEvent != NULL)
		{
			CloseHandle(m_hEvent);
			m_hEvent = NULL;
		}
	}

	inline void Wake()
	{
	}

	inline void WakeAll()
	{
	}

	inline bool Wait(CriticalSection* pCS)
	{
		return false;
	}

	inline bool Wait(CriticalSection* pCS, DWORD msTimeout)
	{
		return false;
	}

	inline bool Wait(RwLock* pRWL, bool isReadLock)
	{
		if(isReadLock)
			pRWL->UnlockRead();
		else
			pRWL->UnlockWrite();

		return false;
	}

	inline bool Wait(RwLock* pRWL, bool isReadLock, DWORD msTimeout)
	{
		return false;
	}
};

#endif

class SimpleEvent
{
	HANDLE m_hEvent;

public:
	inline SimpleEvent()
	{
		m_hEvent = NULL;
	}

#ifdef _DEBUG
	inline ~SimpleEvent() { SC_ASSERT(m_hEvent == NULL); }
#endif

	// auto reset event is default and preferred.
	// manual reset feature may be removed from this class in the future.
	// if you really want the manual reset event, use conditional variable; it's more reliable.
	inline bool Create(bool bInitialState, bool bManualReset =false)
	{
		m_hEvent = CreateEvent(NULL, bManualReset, bInitialState, NULL);
		return (m_hEvent != NULL);
	}

	inline void Destroy()
	{
		if(m_hEvent != NULL)
		{
			CloseHandle(m_hEvent);
			m_hEvent = NULL;
		}
	}

	inline void SetSignal()
	{
		SetEvent(m_hEvent);
	}

	inline void ResetSignal()
	{
		ResetEvent(m_hEvent);
	}

	inline void Wait()
	{
		WaitForSingleObject(m_hEvent, INFINITE);
	}

	inline bool Wait(DWORD nTimeoutMS)
	{
		DWORD ret = WaitForSingleObject(m_hEvent, nTimeoutMS);
		return (ret == WAIT_OBJECT_0);
	}
};


#else
#include "unix_sync.h"
#endif

template<typename LockT>
class AutoLock
{
	LockT& m_lck;

public:
	AutoLock(LockT& lck) : m_lck(lck)
	{
		m_lck.Lock();
	}

	~AutoLock()
	{
		m_lck.Unlock();
	}
};

typedef AutoLock<CriticalSection> AutoCsLock;
typedef AutoLock<SpinLock> AutoSpinLock;

//////////////////////////////////////////////////////////////////////////////
// Simple Lockless Containers

#ifndef MEMORY_ALLOCATION_ALIGNMENT
#ifdef SC_64BIT
#define MEMORY_ALLOCATION_ALIGNMENT		16
#else
#define MEMORY_ALLOCATION_ALIGNMENT		8
#endif
#endif

struct SC_DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) LocklessEntryT
{
	LocklessEntryT* _LocklessNext_;
};

#ifdef __arm__

// (My ARM machine doesn't support Double CAS)

class LocklessStack sealed
{
	CriticalSection m_cs;
	LocklessEntryT* m_pHead;

public:
	LocklessStack()
	{
		m_cs.Create();
		m_pHead = NULL;
	}

	~LocklessStack()
	{
		m_cs.Destroy();
	}

	LocklessEntryT* Pop()
	{
		LocklessEntryT* pRet;

		m_cs.Lock();
		if(m_pHead == NULL)
		{
			pRet = NULL;
		}
		else
		{
			pRet = m_pHead;
			m_pHead = m_pHead->_LocklessNext_;
		}
		m_cs.Unlock();

		return pRet;
	}

	void Push(LocklessEntryT* pEntry)
	{
		m_cs.Lock();
		pEntry->_LocklessNext_ = m_pHead;
		m_pHead = pEntry;
		m_cs.Unlock();
	}
};

// TODO
class LocklessQueue sealed
{
	struct QNode
	{
		LocklessEntryT* pNode;
	};

	QNode m_head;
	QNode m_tail;
	ATOMIC32 m_nSize;

public:
	void Create(LocklessEntryT* pEmptyDummyNode)
	{
		m_nSize = 0;
	}

	LocklessEntryT* Uninit()
	{
		return NULL;
	}

	inline int NumEntries() const
	{
		return m_nSize;
	}

	void Enqueue(LocklessEntryT* pEntry)
	{
	}

	LocklessEntryT* Dequeue(OUT LocklessEntryT** ppNodeToFree)
	{
		return NULL;
	}
};

#else

#if defined(SC_64BIT) && defined(_MSC_VER) && _MSC_VER <= 1400
#	error For x64 build, Visual C++ 2008 or later is required. 
#endif

#if defined(_WIN32) && _WIN32_WINNT >= 0x0501

class LocklessStack sealed
{
	SLIST_HEADER m_head;

public:
	LocklessStack()
	{
		InitializeSListHead(&m_head);
	}

	~LocklessStack()
	{
	}

	inline LocklessEntryT* Pop()
	{
		return (LocklessEntryT*) InterlockedPopEntrySList(&m_head);
	}

	inline void Push(LocklessEntryT* pEntry)
	{
		InterlockedPushEntrySList(&m_head, (SLIST_ENTRY*) pEntry);
	}

	inline int GetSize()
	{
		return QueryDepthSList(&m_head);
	}
};

#else

class LocklessStack sealed
{
	union SC_DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) LocklessStackHeadT
	{
#ifdef SC_64BIT
		__m128 a128;
		ATOMIC64 Alignment[2];
#else
		ATOMIC64 Alignment;
#endif

		struct
		{
			LocklessEntryT* pNext;
#ifdef SC_64BIT
			DWORD Depth, Sequence;
#else
			WORD Depth, Sequence;
#endif
		} s;
	};

	LocklessStackHeadT m_head;

public:
	LocklessStack()
	{
#ifdef SC_64BIT
		m_head.Alignment[0] = m_head.Alignment[1] = 0;
		ASSERT(sizeof(m_head) == 16);
#else
		m_head.Alignment = 0;
#endif
	}

	inline int GetSize() const
	{
		return m_head.s.Depth;
	}

	LocklessEntryT* Pop()
	{
		LocklessEntryT* pRet;
		LocklessStackHeadT oldHeader;
		volatile LocklessStackHeadT newHeader;

		do
		{
			oldHeader = m_head;
			pRet = m_head.s.pNext;
			if(pRet == NULL)
				return NULL;

			newHeader.s.pNext = pRet->_LocklessNext_;
			newHeader.s.Depth = m_head.s.Depth -1;
			newHeader.s.Sequence = m_head.s.Sequence +1;
		}
#ifdef SC_64BIT
		while(_InterlockedCompareExchange128(m_head.Alignment,
			newHeader.Alignment[1], newHeader.Alignment[0], oldHeader.Alignment) == 0);
#else
		while(_InterlockedCompareExchange64(&(m_head.Alignment),
			newHeader.Alignment, oldHeader.Alignment) != oldHeader.Alignment);
#endif

		return pRet;
	}

	void Push(LocklessEntryT* pEntry)
	{
		LocklessStackHeadT oldHeader;
		volatile LocklessStackHeadT newHeader;

		newHeader.s.pNext = pEntry;
		do
		{
			oldHeader = m_head;
			pEntry->_LocklessNext_ = m_head.s.pNext;
			newHeader.s.Depth = m_head.s.Depth +1;
			newHeader.s.Sequence = m_head.s.Sequence +1;
		}
#ifdef SC_64BIT
		while(_InterlockedCompareExchange128(m_head.Alignment,
			newHeader.Alignment[1], newHeader.Alignment[0], oldHeader.Alignment) == 0);
#else
		while(_InterlockedCompareExchange64(&(m_head.Alignment),
			newHeader.Alignment, oldHeader.Alignment) != oldHeader.Alignment);
#endif
	}
};

#endif	// _WIN32

SC_DECLARE_INTERFACE(ILocklessQueueListener)
{
	virtual LocklessEntryT* GetQueueNodeItem() =0;
	virtual void RemoveQueueNodeItem(LocklessEntryT* pItemToFree) =0;
	virtual void CopyQueueNodeData(const LocklessEntryT* pItem, void* param) =0;
};

class LocklessQueue sealed
{
	union SC_DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) QNode
	{
#ifdef SC_64BIT
		__m128 a128;
		ATOMIC64 Alignment[2];
#else
		ATOMIC64 Alignment;
#endif

		struct
		{
			LocklessEntryT* pNode;
			LONG nSequence;
		} s;
	};

	QNode m_head;
	QNode m_tail;
	ILocklessQueueListener* m_pListener;
	ATOMIC32 m_nSize;

public:
	void Create(ILocklessQueueListener* pListener)
	{
		m_pListener = pListener;
		m_nSize = 0;

#ifdef SC_64BIT
		m_head.Alignment[0] = m_head.Alignment[1] = 0;
		m_tail.Alignment[0] = m_tail.Alignment[1] = 0;
#else
		m_head.Alignment = 0;
		m_tail.Alignment = 0;
#endif

		m_head.s.pNode = pListener->GetQueueNodeItem();
		m_head.s.pNode->_LocklessNext_ = NULL;
		m_tail.s.pNode = m_head.s.pNode;
	}

	void Destroy()
	{
		SC_ASSERT(m_nSize == 0);
		if(m_head.s.pNode != NULL)
			m_pListener->RemoveQueueNodeItem(m_head.s.pNode);
	}

	inline int GetSize() const
	{
		return m_nSize;
	}

	// pEntry must be an item which can be removed by ILocklessQueueListener::RemoveQueueItem()
	void Push(LocklessEntryT* pEntry)
	{
		QNode tail, temp, next;

		next.s.pNode = pEntry;
		next.s.pNode->_LocklessNext_ = NULL;

		for(;;)
		{
			tail = m_tail;

			// If the node that the tail points to is the last node
			// then update the last node to point at the new node.
			if(_InterlockedCompareExchangePointer((void**)&(m_tail.s.pNode->_LocklessNext_), next.s.pNode, NULL) == NULL)
			{
				// If the tail points to what we thought was the last node
				// then update the tail to point to the new node
				next.s.nSequence = tail.s.nSequence +1;
#ifdef SC_64BIT
				_InterlockedCompareExchange128(m_tail.Alignment, next.Alignment[1], next.Alignment[0], tail.Alignment);
#else
				_InterlockedCompareExchange64(&(m_tail.Alignment), next.Alignment, tail.Alignment);
#endif
				break;
			}
			else
			{
				// Since the tail does not point at the last node,
				// need to keep updating the tail until it does.
				temp.s.pNode = m_tail.s.pNode->_LocklessNext_;
				temp.s.nSequence = tail.s.nSequence +1;
#ifdef SC_64BIT
				_InterlockedCompareExchange128(m_tail.Alignment, temp.Alignment[1], temp.Alignment[0], tail.Alignment);
#else
				_InterlockedCompareExchange64(&(m_tail.Alignment), temp.Alignment, tail.Alignment);
#endif
			}
		}
		
		_InterlockedIncrement(&m_nSize);
	}

	bool Pop(void* param)
	{
		QNode head, tail, next;

		if(m_nSize == 0)
			return false;

		for(;;)
		{
			head = m_head;
			next.s.pNode = head.s.pNode->_LocklessNext_;
			tail.s.nSequence = m_tail.s.nSequence;

			// Verify that we did not get the pointers in the middle of another update.
			if(head.s.nSequence != m_head.s.nSequence)
				continue;
			
			// Check if the queue is empty
			if(head.s.pNode == m_tail.s.pNode)
			{
				if(NULL == next.s.pNode)
				{
					// queue is empty
					return false;
				}

				// Special case if the queue has nodes but the tail
				// is just behind. Move the tail off of the head.
				tail.s.pNode = head.s.pNode;
				next.s.nSequence = tail.s.nSequence +1;
#ifdef SC_64BIT
				_InterlockedCompareExchange128(m_tail.Alignment, next.Alignment[1], next.Alignment[0], tail.Alignment);
#else
				_InterlockedCompareExchange64(&(m_tail.Alignment), next.Alignment, tail.Alignment);
#endif
			}
			else if(NULL != next.s.pNode)
			{
				// Move the head pointer, effectively removing the node
				next.s.nSequence = head.s.nSequence +1;
//				m_pListener->CopyQueueNodeData(next.s.pNode, param);

#ifdef SC_64BIT
				if(_InterlockedCompareExchange128(m_head.Alignment, next.Alignment[1], next.Alignment[0], head.Alignment) != 0)
#else
				if(_InterlockedCompareExchange64(&(m_head.Alignment), next.Alignment, head.Alignment) == head.Alignment)
#endif
				{
					_InterlockedDecrement(&m_nSize);
					m_pListener->CopyQueueNodeData(next.s.pNode, param);
					m_pListener->RemoveQueueNodeItem(head.s.pNode);
					return true;
				}
			}
		}		
	}
};

#endif	// __arm__

SC_NAMESPACE_END
