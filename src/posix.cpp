#include "stdafx.h"
#include "scimpl.h"

CompletedIoQueue g_cioq;
PollerWaker g_pollerWaker;

#ifdef _WIN32

#pragma comment(lib, "../../../extern/PosixWin32/pthreadVSE2.lib")

#else

const char* GetWsaErrorString(int nErrorCode)
{
	static char unknown_error[16];

	switch(nErrorCode)
	{
	case EACCES:
		return "EACCES";
	case EIO:
		return "EIO";
	case EADDRINUSE:
		return "EADDRINUSE";
	case EADDRNOTAVAIL:
		return "EADDRNOTAVAIL";
	case EAFNOSUPPORT:
		return "EAFNOSUPPORT";
	case EDESTADDRREQ:
		return "EDESTADDRREQ";
	case EINTR:
		return "EINTR";
	case EFAULT:
		return "EFAULT";
#ifndef _WIN32
	case EINPROGRESS:
		return "EINPROGRESS";
	case EMSGSIZE:
		return "EMSGSIZE";
#endif
	case EINVAL:
		return "EINVAL";
	case EBADF:
		return "EBADF";
	case ENOENT:
		return "ENOENT";
	case ENOMEM:
		return "ENOMEM";
	case ENOTDIR:
		return "ENOTDIR";
	case EMFILE:
		return "EMFILE";
	case ENFILE:
		return "ENFILE";
	case ESRCH:
		return "ESRCH";
	case ENOTSOCK:
		return "ENOTSOCK";
	case EOPNOTSUPP:
		return "EOPNOTSUPP";
/*
	case _IO_PENDING:
		return _T("_IO_PENDING");
	case _OPERATION_ABORTED:
		return _T("_OPERATION_ABORTED");
*/
	default:
		StringCchPrintfA(unknown_error, 16, "E(%d)", nErrorCode);
		return unknown_error;
	}
}

DWORD GetTickCount()
{
	struct timeval curr;
	gettimeofday(&curr, NULL);
	return (curr.tv_sec * 1000) + (curr.tv_usec / 1000);
}

BOOL PathAppend(LPTSTR pszPath, LPCTSTR pszMore)
{
	int len = _tcslen(pszPath);

	if(len > 0)
	{
		if(pszPath[len -1] != '/')
		{
			pszPath[len++] = '/';
		}

		_tcscpy(&pszPath[len], pszMore);
		return TRUE;
	}

	return FALSE;
}

PollerWaker::PollerWaker()
{
	m_aPipes[0] = m_aPipes[1] = 0;
	m_bNeedReconfigTimer = false;
}

PollerWaker::~PollerWaker()
{
	ASSERT(m_aPipes[0] == 0 && m_aPipes[0] == 0);
}

bool PollerWaker::Create()
{
#ifdef HAVE_PIPE2
	int ret = pipe2(m_aPipes, O_NONBLOCK);
#else
	int ret = pipe(m_aPipes);
#endif

	if(ret != 0)
	{
		Log(LOG_FATAL, "pipe() failed: %d", errno);
		return false;
	}

	AssociateWithPoller(m_aPipes[0], this);
	return true;
}

void PollerWaker::Destroy()
{
	close(m_aPipes[0]);
	close(m_aPipes[1]);
	m_aPipes[0] = m_aPipes[1] = 0;
}

void PollerWaker::Wake()
{
	char buf[8];

	int ret = send(m_aPipes[1], buf, 1, 0);
	if(ret < 0 && g_bServerRunning)
	{
		Log(LOG_FATAL, "PollerWaker::Wake send() failed: %d %s\n",
			errno, GetWsaErrorString(errno));
	}
}

void PollerWaker::IO_OnReadable()
{
	char buf[8];

	m_bNeedReconfigTimer = true;
	recv(m_aPipes[0], buf, 8, 0);
}

void PollerWaker::IO_OnWritable()
{
	ASSERT(false);
}

void PollerWaker::IO_OnException()
{
	Log(LOG_FATAL, "PollerWalker got exception.\n");
	g_core.Shutdown();
}

#endif

bool MakeSocketNonBlock(SOCKET s, const char* desc)
{
	return true;
#ifdef _WIN32
	u_long nNoBlock = 1;
	if(SOCKET_ERROR == ioctlsocket(s, FIONBIO, &nNoBlock))
#else
	if(SOCKET_ERROR == fcntl(s, F_SETFL, O_NONBLOCK | fcntl(s, F_GETFL, 0)))
#endif
	{
		int nLastError = WSAGetLastError();
		Log(LOG_ERROR, "Failed to make socket(%s) non-block: %d %s\n", desc,
			nLastError, GetWsaErrorString(nLastError));
		return false;
	}

	return true;
}

void EventLog_Error(const TCHAR* format, ...)
{
	TCHAR szBuffer[1024];
	va_list ap;

	va_start(ap, format);
	StringCchVPrintf(szBuffer, 1024, format, ap);
	va_end(ap);

	_tprintf(_T("%s"), szBuffer);
}

void EventLog_LogReport(LPCTSTR msg)
{
	_tprintf(_T("%s"), msg);
}

int ServerCore::GetMemoryLoad()
{
	return 0;
}

int ServerCore::GetCpuLoad(char aUsage[])
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

void ServerPart::IO_OnWritable()
{
	ASSERT(!"ServerPart::IO_OnWritable called.");
}

void ServerPart::IO_OnException()
{
	ASSERT(!"ServerPart::IO_OnException called.");
}

void ServerPart::IO_OnReadable()
{
	struct sockaddr_in remote_addr;
	TcpSocket* pSocket;

#ifndef _WIN32
//_checkAgain:
#endif

	socklen_t addr_size = sizeof(remote_addr);
	SOCKET hNewSocket = accept(m_hListenSocket, (struct sockaddr*) &remote_addr, &addr_size);

	if(hNewSocket < 0)
	{
		int nErrorCode = WSAGetLastError();
		if(nErrorCode != EAGAIN && nErrorCode != EWOULDBLOCK)
		{
			Log(LOG_FATAL, "accept failed: err=%d, %s\n", nErrorCode, GetWsaErrorString(nErrorCode));
		}

		return;
	}

	if(m_nConcurrentConn >= m_nMaxSockets)
	{
		Log(LOG_WARN, "[%s] Exceeded max sockets limit %d. Discard accepted socket.\n",
			m_szName, m_nMaxSockets);
		closesocket(hNewSocket);
		return;
	}
	
	pSocket = g_core.GetFreeAcceptSocket();
	ASSERT(pSocket != NULL);

	if(pSocket->Accept(this, hNewSocket, remote_addr))
	{
		AssociateWithPoller(hNewSocket, pSocket);

		m_PartLock.Lock();
		m_BusySockets.push_back(pSocket);
		m_PartLock.Unlock();
	}
	else
	{
		g_core.DiscardAcceptSocket(pSocket);
	}

#ifndef _WIN32
	// epoll may send one accept event for two or more socket, so check it again.
	// but it is not necessary for win32 select() type.
//	goto _checkAgain;
#endif
}

void ServerPart::OnTcpSocketClosed(TcpSocket* pSocket)
{
	m_PartLock.Lock();
	
	std::list<TcpSocket*>::iterator itr = find(m_BusySockets.begin(), m_BusySockets.end(), pSocket);
	if(itr == m_BusySockets.end())
	{
		Log(LOG_FATAL, "ServerPart: socket(%p) not found in busy list.\n", pSocket);
	}
	else
	{
		m_BusySockets.erase(itr);
	}
	m_PartLock.Unlock();

	g_core.DiscardAcceptSocket(pSocket);
}

//////////////////////////////////////////////////////////////////////////////

DWORD ServerCore::OnTimeout()
{
	LARGE_INTEGER now;
	DWORD dwNextWait;
	std::priority_queue<TTimerItem> addjobs;
	std::vector<TTimerItem> timeouts;

	now.QuadPart = GetTickCount() + USER_TIMER_MINIMUM;

	m_csTimer.Lock();
	{
		for(std::list<TTimerItem>::iterator itr = m_listTimers.begin();
			itr != m_listTimers.end();)
		{
			TTimerItem& ti = *itr;

			if(ti.nExpireTime.QuadPart <= now.QuadPart)
			{
				g_cioq.Put(ti.pListener, ti.nTimerID);

				if(ti.bRepeat)
				{
					ti.nExpireTime.QuadPart += ti.nDefaultTimeout;
					addjobs.push(ti);
				}

//				m_listTimers.erase(itr++);
				itr = m_listTimers.erase(itr);
				continue;
			}

			break;
		}

		dwNextWait = _InsertTimerJobs(addjobs);
	}
	m_csTimer.Unlock();

	return dwNextWait;
}

void ServerCore::OnServerShutdown()
{
	g_pollerWaker.Wake();
}

void ServerCore::CreateMainMonitorEvents()
{
}


bool ServerCore::CreateNetworkerThreads(int nThreadCnt)
{
	for(int i=0; i<nThreadCnt; i++)
	{
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

		int err = pthread_create(&m_aWorkThreadHandles[i], &attr, TcpSocket::WorkerThreadProc, NULL);
		pthread_attr_destroy(&attr);
		if(err != 0)
		{
			Log(LOG_FATAL, "IocpWorker: pthread_create() failed\n");
			return false;
		}
		m_aWorkThreadIDs[i] = 1;// flag for created
		m_nWorkThreadCount++;
	}	

	return true;
}

void ServerCore::TerminateNetworkThreads()
{
	int i;

	Log(LOG_SYSTEM, _T("Terminate %d network thread(s)\n"), m_nWorkThreadCount);

	for(i=0; i<m_nWorkThreadCount; i++)
	{
		g_cioq.PutQuit();
	}

	for(i=0; i<m_nWorkThreadCount; i++)
	{
		pthread_join(m_aWorkThreadHandles[i], NULL);
	}	
}

static void OnSignal(int nSignal)
{
	g_bServerRunning = false;
	printf("Got signal: %d\n", nSignal);
}


void ServerCore::EnterMainLoop()
{
#ifdef _WIN32
	extern BOOL WINAPI CtrlHandler(DWORD dwCtrlType);
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
#else
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;// | SA_RESETHAND;
	sa.sa_handler = OnSignal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	printf("Signal sigaction\n");

#endif

	g_cioq.EnterPoller();

	Log(LOG_SYSTEM, "Graceful ServerCore shutdown.\n");
}

//////////////////////////////////////////////////////////////////////////////

CompletedIoQueue::CompletedIoQueue()
{
}

bool CompletedIoQueue::Create()
{
	m_nWaitCount = 1;
	m_queue.Create(this);
	m_condivar.Create();
	m_cs.Create();

	return true;
}

void CompletedIoQueue::Destroy()
{
	m_queue.Destroy();
	m_cs.Destroy();
	m_condivar.Destroy();
}

Sync::LocklessEntryT* CompletedIoQueue::GetQueueNodeItem()
{
	// get a free item
	QNodeItem* pItem = (QNodeItem*) m_freeItems.Pop();
	while(pItem == NULL)
	{
		// alloc new block
		int nAlloc = 1024 * 4;
		int cbItem = sizeof(QNodeItem);
		BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
		while(nAlloc-- > 0)
		{
			m_freeItems.Push((Sync::LocklessEntryT*)pFree);
			pFree += cbItem;
		}

		pItem = (QNodeItem*) m_freeItems.Pop();
	}

	return pItem;
}

void CompletedIoQueue::RemoveQueueNodeItem(Sync::LocklessEntryT* pItemToFree)
{
	m_freeItems.Push(pItemToFree);
}

void CompletedIoQueue::CopyQueueNodeData(const Sync::LocklessEntryT* pItem, void* param)
{
	memcpy(param, &((QNodeItem*)pItem)->data, sizeof(CompletedIoInfo));
}

void CompletedIoQueue::Get(CompletedIoInfo* pOut)
{
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
}

void CompletedIoQueue::_Put(QNodeItem* pItem)
{
	m_queue.Push(pItem);

	ATOMIC32 cnt = _InterlockedDecrement(&m_nWaitCount);
	_InterlockedIncrement(&m_nWaitCount);

	if(cnt >= 1)
		m_condivar.Wake();
}

void CompletedIoQueue::Put(int type, TcpSocket* p)
{
	QNodeItem* pItem = (QNodeItem*) GetQueueNodeItem();
	pItem->data.type = type;
	pItem->data.pSocket = p;
	_Put(pItem);
}

void CompletedIoQueue::Put(ITimerListener* p, int nTimerID)
{
	QNodeItem* pItem = (QNodeItem*) GetQueueNodeItem();
	pItem->data.type = CIO_TIMEOUT;
	pItem->data.param = nTimerID;
	pItem->data.pTimerListener = p;
	_Put(pItem);
}

void CompletedIoQueue::PutQuit()
{
	QNodeItem* pItem = (QNodeItem*) GetQueueNodeItem();
	pItem->data.type = CIO_CORE_SHUTDOWN;
	_Put(pItem);
}
