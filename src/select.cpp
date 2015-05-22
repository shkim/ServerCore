#include "stdafx.h"
#include "scimpl.h"

// select() poller is not intended to be used in real UNIX environment,
// this is for dev&test of ServerCore POSIX version in Visual Studio.
// (Real UNIX should use epoll or kqueue poller.)

//#if !defined(HAVE_SYS_EPOLL_H) && !defined(HAVE_SYS_EVENT_H)

struct PollerCommand : public Sync::LocklessEntryT
{
	SOCKET hSocket;
	IoEventListener* pListener;
	bool isAdd;
};

//static Sync::LocklessStack s_stkPollerCommands;
//static Sync::LocklessStack s_stkFreePcItems;

static Sync::CriticalSection s_csCmdLock;
static std::queue<PollerCommand> s_qPollerCmds;

struct SocketItem
{
	SOCKET hSocket;
	IoEventListener* pListener;
};

#define MAX_SELECT_SOCKETS		256

static SocketItem s_aPollingSockets[MAX_SELECT_SOCKETS];
static int s_nPollingSocketCount = 0;

static SOCKET s_aWatchSendableSockets[MAX_SELECT_SOCKETS];
static int s_nWatchSendableCount = 0;

bool ServerCore::CreateIOCP()
{	
	ASSERT(g_hCompPort == INVALID_HANDLE_VALUE);

	s_csCmdLock.Create();
/*
	int nAlloc = 1024 * 4;
	int cbItem = sizeof(PollerCommand);
	BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
	while(nAlloc-- > 0)
	{
		s_stkFreePcItems.Push((Sync::LocklessEntryT*)pFree);
		pFree += cbItem;
	}
*/
	return true;
}

void ServerCore::CloseIOCP()
{
	ASSERT(g_hCompPort == INVALID_HANDLE_VALUE);

	s_csCmdLock.Destroy();
}

bool AssociateWithPoller(SOCKET hSocket, IoEventListener* pListener)
{
	Log(LOG_INFO, "AssociateWithPoller(sock=%x,%p)\n", hSocket, pListener);

	ASSERT(s_nPollingSocketCount < MAX_SELECT_SOCKETS);

/*
	PollerCommand* pNewCmd = (PollerCommand*) s_stkFreePcItems.Pop();
	ASSERT(pNewCmd != NULL);

	pNewCmd->isAdd = true;
	pNewCmd->hSocket = hSocket;
	pNewCmd->pListener = pObject;
	s_stkPollerCommands.Push(pNewCmd);
*/

	PollerCommand cmd;
	cmd.isAdd = true;
	cmd.hSocket = hSocket;
	cmd.pListener = pListener;

	s_csCmdLock.Lock();
	s_qPollerCmds.push(cmd);
	s_csCmdLock.Unlock();

	return true;
}

void SetSendableInterestWithPoller(SOCKET hSocket, IoEventListener* pListener, bool bNotifySendable)
{
	PollerCommand cmd;
	cmd.isAdd = bNotifySendable;
	cmd.hSocket = hSocket;
	cmd.pListener = NULL;

	s_csCmdLock.Lock();
	s_qPollerCmds.push(cmd);
	s_csCmdLock.Unlock();
}

void UnassociateWithPoller(SOCKET hSocket, IoEventListener* pObject)
{
	Log(LOG_INFO, "UnAssociateWithPoller(sock=%d,%p)\n", hSocket, pObject);

	PollerCommand cmd;
	cmd.isAdd = false;
	cmd.hSocket = hSocket;
	cmd.pListener = pObject;

	s_csCmdLock.Lock();
	s_qPollerCmds.push(cmd);
	s_csCmdLock.Unlock();
};

//////////////////////////////////////////////////////////////////////////////

void CompletedIoQueue::EnterPoller()
{
	fd_set fdsInput, fdsOutput, fdsExcept;
	struct timeval timeout;
	struct timeval* pTimeout = NULL;
	DWORD dwNextWait;
	int i;
	int ret;

	while(g_bServerRunning)
	{
		FD_ZERO(&fdsInput);
		FD_ZERO(&fdsOutput);
		FD_ZERO(&fdsExcept);

		for(;;)
		{
			s_csCmdLock.Lock();
			if(s_qPollerCmds.empty())
			{
				s_csCmdLock.Unlock();
				break;
			}

			PollerCommand& cmd = s_qPollerCmds.front();
			s_qPollerCmds.pop();
			s_csCmdLock.Unlock();

			if(cmd.isAdd)
			{
				if(cmd.pListener == NULL)
				{
					// sendable notification
					s_aWatchSendableSockets[s_nWatchSendableCount] = cmd.hSocket;
					++s_nWatchSendableCount;
				}
				else
				{
					s_aPollingSockets[s_nPollingSocketCount].hSocket = cmd.hSocket;
					s_aPollingSockets[s_nPollingSocketCount].pListener = cmd.pListener;
					++s_nPollingSocketCount;
				}
			}
			else if(cmd.pListener == NULL)
			{
				for(int i=0; i<s_nWatchSendableCount; i++)
				{
					if(s_aWatchSendableSockets[i] == cmd.hSocket)
					{
						--s_nWatchSendableCount;
						if(i < s_nWatchSendableCount)
						{
							s_aWatchSendableSockets[i] = s_aWatchSendableSockets[s_nWatchSendableCount];
						}

						goto _removeOK;
					}
				}
			}
			else
			{
				for(int i=0; i<s_nPollingSocketCount; i++)
				{
					if(s_aPollingSockets[i].hSocket == cmd.hSocket)
					{
						--s_nPollingSocketCount;
						ASSERT(s_aPollingSockets[i].pListener == cmd.pListener);
						if(i < s_nPollingSocketCount)
						{
							s_aPollingSockets[i].hSocket = s_aPollingSockets[s_nPollingSocketCount].hSocket;
							s_aPollingSockets[i].pListener = s_aPollingSockets[s_nPollingSocketCount].pListener;
						}

						goto _removeOK;
					}
				}

				ASSERT(!"PollerCommand: remove failed");
			}

_removeOK:
			;
		}

		for(i=0; i<s_nPollingSocketCount; i++)
		{
			FD_SET(s_aPollingSockets[i].hSocket, &fdsInput);
			FD_SET(s_aPollingSockets[i].hSocket, &fdsExcept);
		}

		for(i=0; i<s_nWatchSendableCount; i++)
		{
			FD_SET(s_aWatchSendableSockets[i], &fdsOutput);
		}

		ret = select(0, &fdsInput, &fdsOutput, &fdsExcept, pTimeout);

		if(ret < 0)
		{
			int nError = WSAGetLastError();
			Log(LOG_FATAL, "select() failed: %d %s\n", nError, GetWsaErrorString(nError));
			break;
		}

		if(!g_bServerRunning)
			break;	// server shutdown

		if(ret == 0)
		{
			// timer timeout
			dwNextWait = g_core.OnTimeout();
			goto _setupTimeout;
		}

		for(i=0; i<s_nPollingSocketCount; i++)
		{
			if(FD_ISSET(s_aPollingSockets[i].hSocket, &fdsOutput))
			{
				s_aPollingSockets[i].pListener->IO_OnWritable();
			}

			if(FD_ISSET(s_aPollingSockets[i].hSocket, &fdsInput))
			{
				s_aPollingSockets[i].pListener->IO_OnReadable();
			}

			if(FD_ISSET(s_aPollingSockets[i].hSocket, &fdsExcept))
			{
				s_aPollingSockets[i].pListener->IO_OnException();
			}
		}

		if(g_pollerWaker.m_bNeedReconfigTimer)
		{
			g_pollerWaker.m_bNeedReconfigTimer = false;
			dwNextWait = g_core.ReconfigureTimers();

_setupTimeout:
			if(dwNextWait == INFINITE)
			{
				pTimeout = NULL;
			}
			else
			{
				pTimeout = &timeout;
				if(dwNextWait >= 1000)
				{
					timeout.tv_sec = dwNextWait / 1000;
					timeout.tv_usec = (dwNextWait - (timeout.tv_sec * 1000)) * 1000;
				}
				else
				{
					timeout.tv_sec = 0;
					timeout.tv_usec = dwNextWait * 1000;
				}
			}
		}
		else if(pTimeout != NULL)
		{
			dwNextWait = g_core.OnTimeout();
			goto _setupTimeout;
		}
	}
}


















/*

#define MAX_ASSOCIATION_LIMIT		62

static DWORD s_nEventCount = 0;
static WSAEVENT s_aEventList[MAX_ASSOCIATION_LIMIT];
struct EventListenerItem
{
	SOCKET hSocket;
	IoEventListener* pListener;
};
static EventListenerItem s_aEventItems[MAX_ASSOCIATION_LIMIT];

bool AssociateWithPoller(SOCKET hSocket, IoEventListener* pObject)
{
	Log(LOG_DEBUG, "AssociateWithPoller(sock=%d,%p)\n", hSocket, pObject);

	long evtFlags;

	ServerPart* pSvr = dynamic_cast<ServerPart*>(pObject);
	if(pSvr != NULL)
	{
		evtFlags = FD_ACCEPT;
	}
	else
	{
		TcpSocket* pCli = dynamic_cast<TcpSocket*>(pObject);
		if(pCli != NULL)
		{
			evtFlags = FD_READ | FD_WRITE | FD_CLOSE;
		}
		else
		{
			Log(LOG_FATAL, "AssociateWithPoller: unknown object type.\n");
			return false;
		}
	}

	if(s_nEventCount >= MAX_ASSOCIATION_LIMIT)
	{
		Log(LOG_FATAL, "Reached maximum multiplexing count.\n");
		return false;
	}

	WSAEVENT hEvent = WSACreateEvent();
	s_aEventList[s_nEventCount] = hEvent;
	s_aEventItems[s_nEventCount].hSocket = hSocket;
	s_aEventItems[s_nEventCount].pListener = pObject;
	s_nEventCount++;

	WSAEventSelect(hSocket, hEvent, evtFlags);

	return true;
}

//////////////////////////////////////////////////////////////////////////////

void IoEventQueue::Dispatch()
{
	WSANETWORKEVENTS networkEvents;

	while(!g_bServerShutdown)
	{
		DWORD ret = WSAWaitForMultipleEvents(s_nEventCount, s_aEventList,
			FALSE, WSA_INFINITE, FALSE);

		if(g_bServerShutdown)
			break;

		if(ret >=  WSA_WAIT_EVENT_0 && ret < (WSA_WAIT_EVENT_0 + s_nEventCount))
		{
			int idx = ret - WSA_WAIT_EVENT_0;
			int err = WSAEnumNetworkEvents(s_aEventItems[idx].hSocket,
				s_aEventList[idx], &networkEvents);

			if(err == SOCKET_ERROR)
			{
				int nError = WSAGetLastError();
				Log(LOG_FATAL, "WSAEnumNetworkEvents failed: %d %s\n", nError, GetWsaErrorString(nError));
				break;
			}

			IoEventListener* pListener = s_aEventItems[idx].pListener;

			if(networkEvents.lNetworkEvents & FD_ACCEPT)
			{
				pListener->IO_OnCanRead();
			}
			else if((networkEvents.lNetworkEvents & FD_READ)
			|| (networkEvents.lNetworkEvents & FD_CLOSE))
			{
				pListener->IO_OnCanRead();
			}
			else if(networkEvents.lNetworkEvents & FD_WRITE)
			{
				pListener->IO_OnCanWrite();
			}

_checkKsAgain:
			KickedSocket* pKS = (KickedSocket*) InterlockedPopEntrySList(&s_KickedSocketList);
			if(pKS != NULL)
			{
				for(DWORD i=0; i<s_nEventCount; i++)
				{
					if(s_aEventItems[i].hSocket == pKS->pSocket->m_hSocket)
					{
						pKS->pSocket->Kick();

						InterlockedPushEntrySList(&s_FreeKsItemList, &pKS->entry);
						if(i < --s_nEventCount)
						{
							s_aEventList[i] = s_aEventList[s_nEventCount];
							s_aEventItems[i] = s_aEventItems[s_nEventCount];
						}

						goto _checkKsAgain;
					}
				}

				Log(LOG_FATAL, "Kicked socket(%d) not found in ks-list\n", pKS->pSocket->m_hSocket);
			}
		}
		else
		{
			int nError = WSAGetLastError();
			Log(LOG_FATAL, "WSAWaitForMultipleEvents failed: %d %s\n", nError, GetWsaErrorString(nError));
			break;
		}
	}
}
*/

//#endif
//#endif






//////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

// win32 select() only accepts SOCKET, no pipe.
// so create the socket objects to be polled.

PollerWaker::PollerWaker()
{
	m_hConnectSocket = INVALID_SOCKET;
	m_hAcceptedSocket = INVALID_SOCKET;
	m_bNeedReconfigTimer = false;
}

PollerWaker::~PollerWaker()
{
	ASSERT(m_hConnectSocket == INVALID_SOCKET && m_hAcceptedSocket == INVALID_SOCKET);
}

bool PollerWaker::Create()
{
	const int nListenPort = 59876;		// need random...
	const char* pszName = "PollerWaker";

	struct sockaddr_in sa;
	SOCKET hListenSocket;
	int err;

	ASSERT(m_hAcceptedSocket == INVALID_SOCKET && m_hConnectSocket == INVALID_SOCKET);
	hListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT(hListenSocket != INVALID_SOCKET);

	if(!MakeSocketReuseAddr(hListenSocket, pszName))
		goto _failure;

	// bind socket
	ZeroMemory(&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(nListenPort);

	if(SOCKET_ERROR == bind(hListenSocket, (struct sockaddr*) &sa, sizeof(sa)))
	{
		err = WSAGetLastError();
		Log(LOG_FATAL, "[%s] Couldn't bind listen socket on port %d, error=%d, %s\n",
			pszName, nListenPort, err, GetWsaErrorString(err));
		goto _failure;
	}

	// Start listening on the listening socket
	if(SOCKET_ERROR == listen(hListenSocket, 1))
	{
		err = WSAGetLastError();
		Log(LOG_FATAL, "[%s] listen() failed: %d %s\n",
			pszName, err, GetWsaErrorString(err));
		goto _failure;
	}

	// make listening socket non-blocking
	if(!MakeSocketNonBlock(hListenSocket, pszName))
	{
		goto _failure;
	}

	// connect to server
	m_hConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT(m_hConnectSocket != INVALID_SOCKET);

	if(SOCKET_ERROR == connect(m_hConnectSocket, (sockaddr*) &sa, sizeof(sa)))
	{
		err = WSAGetLastError();
		Log(LOG_FATAL, "[%s] connect() failed: %d %s\n",
			pszName, err, GetWsaErrorString(err));
		goto _failure;
	}

	socklen_t addr_size = sizeof(sa);
	m_hAcceptedSocket = accept(hListenSocket, (struct sockaddr*) &sa, &addr_size);
	if( INVALID_SOCKET == m_hAcceptedSocket)
	{
		err = WSAGetLastError();
		Log(LOG_FATAL, "[%s] accept() failed: %d %s\n",
			pszName, err, GetWsaErrorString(err));
		goto _failure;
	}

	closesocket(hListenSocket);
	AssociateWithPoller(m_hAcceptedSocket, this);
	return true;

_failure:
	closesocket(hListenSocket);
	Destroy();
	return false;
}

void PollerWaker::Destroy()
{
	if(m_hAcceptedSocket != INVALID_SOCKET)
	{
		closesocket(m_hAcceptedSocket);
		m_hAcceptedSocket = INVALID_SOCKET;
	}

	if(m_hConnectSocket != INVALID_SOCKET)
	{
		closesocket(m_hConnectSocket);
		m_hConnectSocket = INVALID_SOCKET;
	}
}

void PollerWaker::Wake()
{
	char buf[8];

	int ret = send(m_hConnectSocket, buf, 1, 0);
	if(ret < 0)
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "PollerWaker::Wake send() failed: %d %s\n",
			nLastError, GetWsaErrorString(nLastError));
	}
}

void PollerWaker::IO_OnReadable()
{
	char buf[8];

	m_bNeedReconfigTimer = true;
	recv(m_hAcceptedSocket, buf, 8, 0);
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
