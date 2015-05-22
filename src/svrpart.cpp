#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

ClientListenerPool::ClientListenerPool()
{
	m_nListenerTypeCount = 0;
	m_nUdpListenerCount = 0;
	m_nBusyObjectCount = 0;
	m_aFrees = NULL;

#ifdef SC_USE_INTELTBB
	m_aTbbPendings = NULL;
#else
	m_aPendings = NULL;
#endif	
}

int ClientListenerPool::Register(ClientListenerCreatorFuncT pfn, const void* param)
{
	int idx = m_nListenerTypeCount++;
	if(idx < MAX_LISTENER_CREATORS)
	{
		m_aListenerCreators[idx].pfn = pfn;
		m_aListenerCreators[idx].param = param;
		return idx;
	}
	
	return 0;
}

void ClientListenerPool::RegisterUdp()
{
	m_nUdpListenerCount++;
}

// After Finalize, more Register call will be disabled.
bool ClientListenerPool::Finalize()
{
	if(m_nListenerTypeCount == 0)
	{
		if(m_nUdpListenerCount > 0)
			return true;

		Log(LOG_FATAL, "No client listener registered.\n");
		return false;
	}

	if(m_nListenerTypeCount == MAX_LISTENER_CREATORS)
	{
		Log(LOG_FATAL, "Too many client listeners (exceeded the hard limit %d)\n", MAX_LISTENER_CREATORS);
		return false;
	}

	m_aFrees = new LocklessStack[m_nListenerTypeCount];
	if(m_aFrees == NULL)
		return false;

#ifdef SC_USE_INTELTBB
	m_aTbbPendings = new TbbLocklessQueue[m_nListenerTypeCount];
#else
	m_aPendings = new LocklessQueue[m_nListenerTypeCount];
	for (int i = 0; i<m_nListenerTypeCount; i++)
	{
		m_aPendings[i].Create(this);
	}
#endif

	// precreate
	for(int i=0; i<m_nListenerTypeCount; i++)
	{
		for(int c=0; c<2; c++)
		{
			ListenerItem* pItem = (ListenerItem*) GetQueueNodeItem();
			pItem->pListener = m_aListenerCreators[i].pfn(m_aListenerCreators[i].param);
			if(pItem->pListener == NULL)
			{
				Log(LOG_FATAL, "Pre-creating ClientListener(#%d) failed.\n", i);
				return false;
			}

			m_aFrees[i].Push(pItem);
		}
	}

	return true;
}

void ClientListenerPool::Destroy()
{
	ListenerItem* pItem;

	if(m_aFrees == NULL)
		return;

	for(int i=0; i<m_nListenerTypeCount; i++)
	{
		for(;;)
		{
			pItem = (ListenerItem*) m_aFrees[i].Pop();
			if(pItem == NULL)
				break;

			pItem->pListener->OnFinalDestruct();
		}

#ifdef SC_USE_INTELTBB
		while (m_aTbbPendings[i].try_pop(pItem))
		{
			pItem->pListener->OnFinalDestruct();
		}
#else
		ITcpClientListener* pListener;
		while (m_aPendings[i].Pop(&pListener))
		{
			pListener->OnFinalDestruct();
		}

		m_aPendings[i].Destroy();
#endif
	}

	delete [] m_aFrees;
	m_aFrees = NULL;

#ifdef SC_USE_INTELTBB
	delete [] m_aTbbPendings;
	m_aTbbPendings = NULL;
#else
	delete [] m_aPendings;
	m_aPendings = NULL;
#endif
}

LocklessEntryT* ClientListenerPool::GetQueueNodeItem()
{
	for(;;)
	{
		ListenerItem* pItem = (ListenerItem*) m_itempool.Pop();
		if(pItem != NULL)
			return pItem;

		int nAlloc = 1024 * 4;
		int cbItem = sizeof(ListenerItem);
		BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);
		while(nAlloc-- > 0)
		{
			m_itempool.Push((LocklessEntryT*)pFree);
			pFree += cbItem;
		}
	}
}

void ClientListenerPool::RemoveQueueNodeItem(LocklessEntryT* pItemToFree)
{
	m_itempool.Push(pItemToFree);
}

void ClientListenerPool::CopyQueueNodeData(const LocklessEntryT* pItem, void* param)
{
	ListenerItem* p = (ListenerItem*) pItem;
	*((ITcpClientListener**)param) = p->pListener;
}

/*
void ClientListenerPool::Discard(ServerPart* pSP, ListenerItem* pItem)
{
	const int nTypeID = pSP->m_nListenerTypeID;
	ASSERT(nTypeID >= 0 && nTypeID < m_nListenerTypeCount);

	pItem->pListener->Ref_Release();
	
	if(pItem->pListener->Ref_GetCount() > 0)
		m_aPendings[nTypeID].Push(pItem);
	else
		m_aFrees[nTypeID].Push(pItem);

	_InterlockedDecrement(&m_nBusyObjectCount);
}

ClientListenerPool::ListenerItem* ClientListenerPool::Get(ServerPart* pSP)
{
	const int nTypeID = pSP->m_nListenerTypeID;
	ASSERT(nTypeID >= 0 && nTypeID < m_nListenerTypeCount);

	ListenerItem* pItem = (ListenerItem*) m_aFrees[nTypeID].Pop();
	if(pItem == NULL)
	{
		ITcpClientListener* pPendingListener;

		if(m_aPendings[nTypeID].Pop(&pPendingListener))
		{
			pItem = (ListenerItem*) GetQueueNodeItem();
			pItem->pListener = pPendingListener;

			if(pPendingListener->Ref_GetCount() == 0)
				goto _found;

			// pending job exists
			Log(LOG_SYSTEM, "Disconnected socket has pending jobs.\n");
			m_aPendings[nTypeID].Push(pItem);
		}

		pItem = (ListenerItem*) GetQueueNodeItem();
		if(pItem != NULL)
		{
			pItem->pListener = m_aListenerCreators[nTypeID].pfn(m_aListenerCreators[nTypeID].param);
			if(pItem->pListener != NULL)
				goto _found;

		}

		Log(LOG_FATAL, "Creating ClientListener(#%d) failed.\n", nTypeID);
		return NULL;
	}

_found:

	ASSERT(pItem->pListener->Ref_GetCount() == 0);
	pItem->pListener->Ref_Retain();
	_InterlockedIncrement(&m_nBusyObjectCount);
	return pItem;
}

*/

void ClientListenerPool::Discard(ServerPart* pSP, ListenerItem* pItem)
{
	const int nTypeID = pSP->m_nListenerTypeID;
	SC_ASSERT(nTypeID >= 0 && nTypeID < m_nListenerTypeCount);

	pItem->pListener->Ref_Release();

	if (pItem->pListener->Ref_GetCount() > 0)
	{
#ifdef SC_USE_INTELTBB
		m_aTbbPendings[nTypeID].push(pItem);
#else
		m_aPendings[nTypeID].Push(pItem);
#endif
	}		
	else
	{
		m_aFrees[nTypeID].Push(pItem);
	}

	_InterlockedDecrement(&m_nBusyObjectCount);
}

ClientListenerPool::ListenerItem* ClientListenerPool::Get(ServerPart* pSP)
{
	const int nTypeID = pSP->m_nListenerTypeID;
	SC_ASSERT(nTypeID >= 0 && nTypeID < m_nListenerTypeCount);

	ListenerItem* pItem = (ListenerItem*) m_aFrees[nTypeID].Pop();
	if(pItem == NULL)
	{
#ifdef SC_USE_INTELTBB
		if(m_aTbbPendings[nTypeID].try_pop(pItem))
#else
		if (m_aPendings[nTypeID].Pop(pItem))
#endif
		{
			if(pItem->pListener->Ref_GetCount() == 0)
				goto _found;

			// pending job exists
			Log(LOG_SYSTEM, "Disconnected socket has pending jobs.\n");
#ifdef SC_USE_INTELTBB
			m_aTbbPendings[nTypeID].push(pItem);
#else
			m_aPendings[nTypeID].Push(pItem);
#endif
		}

		pItem = (ListenerItem*) GetQueueNodeItem();
		if(pItem != NULL)
		{
			pItem->pListener = m_aListenerCreators[nTypeID].pfn(m_aListenerCreators[nTypeID].param);
			if(pItem->pListener != NULL)
				goto _found;
		}

		Log(LOG_FATAL, "Creating ClientListener(#%d) failed.\n", nTypeID);
		return NULL;
	}

_found:

	SC_ASSERT(pItem->pListener->Ref_GetCount() == 0);
	pItem->pListener->Ref_Retain();
	_InterlockedIncrement(&m_nBusyObjectCount);
	return pItem;
}

//////////////////////////////////////////////////////////////////////////////

bool ServerCore::ReadServerPartInfo(IConfigSection* pSvrSect)
{
	const char* pszName;
	int len;

	const char* pszTcpServers = pSvrSect->GetString("Servers");
	const char* pszUdpServers = pSvrSect->GetString("UdpServers");
	if(pszTcpServers == NULL && pszUdpServers == NULL)
	{
		Log(LOG_FATAL, "No 'Servers' or 'UdpServers' directive in config.\n");
		return false;
	}

	StringSplitter ssp(pszTcpServers);
	while(ssp.GetNext(&pszName, &len))
	{
		if(len >= 48)	// sizeof(ServerPart::m_szName)
		{
			char* pNull = const_cast<char*>(pszName);
			pNull[len] = 0;

			Log(LOG_FATAL, "Too long Server name: %s\n", pszName);
			return false;
		}

		ServerPart* pPart = new ServerPart(pszName, len);
		pPart->m_nServerBindFlags |= ServerPart::BIND_TCP;
		m_SvrParts.push_back(pPart);
	}

	ssp.Reset(pszUdpServers);
	while(ssp.GetNext(&pszName, &len))
	{
		if(len >= 48)	// sizeof(ServerPart::m_szName)
		{
			char* pNull = const_cast<char*>(pszName);
			pNull[len] = 0;

			Log(LOG_FATAL, "Too long Server name: %s\n", pszName);
			return false;
		}

		// find if same-named tcp exists
		ServerPart* pPart;
		std::vector<ServerPart*>::iterator itr = m_SvrParts.begin();
		for(; itr != m_SvrParts.end(); ++itr)
		{
			pPart = *itr;
			if(pPart->m_szName[len] == 0 && !strncmp(pPart->m_szName, pszName, len))
			{
				goto _found;
			}
		}

		pPart = new ServerPart(pszName, len);		
		m_SvrParts.push_back(pPart);

_found:
		pPart->m_nServerBindFlags |= ServerPart::BIND_UDP;
	}

	if(m_SvrParts.empty())
	{
		Log(LOG_FATAL, "No Servers specified.\n");
		return false;
	}

	for(size_t i=0; i<m_SvrParts.size(); i++)
	{
		ServerPart* pPart = m_SvrParts.at(i);

		IConfigSection* pSect = g_core.GetConfigSection(pPart->m_szName);
		if(pSect == NULL)
		{
			Log(LOG_FATAL, "No [%s] section found in config.\n", pPart->m_szName);
			return false;
		}

		pPart->m_nListenPort = (unsigned short) pSect->GetInteger("ListenPort");
		if(pPart->m_nListenPort == 0 && g_bServerStartedListening)
		{
			Log(LOG_FATAL, "ListenPort for [%s] is not specified.\n", pPart->m_szName);
			return false;
		}

		pPart->m_pszListenAddress = pSect->GetString("ListenAddress");

		pPart->m_nMaxSockets = pSect->GetInteger("MaxSockets", 1024);
		if(pPart->m_nMaxSockets <= 1)
		{
			Log(LOG_FATAL, "[%s] Invalid MaxSockets value %d\n", pPart->m_szName, pPart->m_nMaxSockets);
			return false;
		}

		pPart->m_nMaxSendBuffersPerConn = pSect->GetInteger("MaxSendBuffersPerConn", 2);
	}

	// check for conflicts
	if(m_SvrParts.size() > 1)
	{
		for(size_t j=0; j<m_SvrParts.size(); j++)
		{
			ServerPart* pPart1 = m_SvrParts.at(j);
			for(size_t k=0; k!=j && k<m_SvrParts.size(); k++)
			{
				ServerPart* pPart2 = m_SvrParts.at(k);

				if(pPart1->m_nListenPort == pPart2->m_nListenPort && pPart1->m_nListenPort != 0)
				{
					Log(LOG_FATAL, "ListenPort for [%s] conflicts with [%s]'s.\n",
						pPart1->m_szName, pPart2->m_szName);
					return false;
				}

				if(_stricmp(pPart1->m_szName, pPart2->m_szName) == 0)
				{
					Log(LOG_FATAL, "Config section name for [%s] conflicts with [%s]'s.\n",
						pPart1->m_szName, pPart2->m_szName);
					return false;
				}
			}
		}
	}

	return true;
}

ServerPart::ServerPart(const char* pszName, int lenName)
{
	StringCchCopyNA(m_szName, sizeof(m_szName), pszName, lenName);
	m_szName[lenName] = 0;

	m_hListenSocket = INVALID_SOCKET;
	m_pUdpSocket = NULL;
	m_pUdpSocketListener = NULL;

	m_nConcurrentConn = 0;
	m_nTotalRecvBytes = 0;
	m_nTotalSentBytes = 0;

	m_nMaxSockets = 0;
	m_nListenerTypeID = -1;
	m_nServerBindFlags = 0;

	m_PartLock.Create();
}

bool ServerPart::CreateListenSocket()
{
	struct sockaddr_in sa;
	SOCKET hSocket;

	SC_ASSERT(IsTCP());

	ZeroMemory(&sa, sizeof(sa));
	if(m_pszListenAddress == NULL)
	{
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
		m_pszListenAddress = "*";
	}
	else
	{
		sa.sin_addr.s_addr = inet_addr(m_pszListenAddress);
		if(sa.sin_addr.s_addr == INADDR_NONE)
		{
			Log(LOG_FATAL, "[%s] Invalid ListenAddress: %s\n", m_szName, m_pszListenAddress);
			return false;
		}
	}

#ifdef SC_PLATFORM_POSIX
	hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	//Log(LOG_DEBUG, "[%s] Listen socket created (fd=%d)\n", m_szName, hSocket);
#else
	hSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
		NULL, 0, WSA_FLAG_OVERLAPPED);
#endif

	if(hSocket == INVALID_SOCKET)
	{
		Log(LOG_FATAL, "WSASocket(for listen) failed.\n");
		return false;
	}

	m_hListenSocket = hSocket;

	if(!MakeSocketReuseAddr(hSocket, m_szName))
		return false;

#ifndef SC_PLATFORM_POSIX
	AssociateDeviceWithCompletionPort(g_hCompPort, (HANDLE) hSocket, 0);
#endif

	// bind socket
	sa.sin_port = htons(m_nListenPort);
	sa.sin_family = AF_INET;

	if(SOCKET_ERROR == bind(hSocket, (struct sockaddr*) &sa, sizeof(sa)))
	{
		Log(LOG_FATAL, "[%s] Couldn't bind listen socket on %s:%d (error=%s)\n",
			m_szName, m_pszListenAddress, m_nListenPort, GetWsaErrorString(WSAGetLastError()));
		return false;
	}

	// Start listening on the listening socket
	if(SOCKET_ERROR == listen(hSocket, 5))//SOMAXCONN))
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "[%s] listen() failed: %d %s\n",
			m_szName, nLastError, GetWsaErrorString(nLastError));
		return false;
	}

#ifdef SC_PLATFORM_POSIX
	// make listening socket non-blocking
	if(!MakeSocketNonBlock(hSocket, m_szName))
	{
		return false;
	}

	AssociateWithPoller(hSocket, this);
#ifdef _WIN32
	g_pollerWaker.Wake();
#endif
#endif

	Log(LOG_SYSTEM, "[%s] socket(%x) listen to %s:%d\n", m_szName, m_hListenSocket, m_pszListenAddress, m_nListenPort);
	return true;
}

// called on init stage
bool ServerPart::CreateAcceptSockets()
{
	SC_ASSERT(IsTCP());

	IConfigSection* pSect = g_core.GetConfigSection(m_szName);
	SC_ASSERT(pSect != NULL);

	int nInitialSockets = pSect->GetInteger("InitialSockets", 16);
	if(nInitialSockets <= 0)
	{
		Log(LOG_FATAL, "Invalid number for socket precreation: %d\n", nInitialSockets);
		return false;
	}

	m_nPrecreateSockets = pSect->GetInteger("PrecreateSockets", 16);

#ifndef SC_PLATFORM_POSIX
	Log(LOG_SYSTEM, "[%s] Creating %d sockets.\n", m_szName, nInitialSockets);
	m_AcceptSockets.reserve(nInitialSockets);

	while(nInitialSockets-- > 0)
	{
		TcpSocket* pSocket = new TcpSocket();

		if(0 != pSocket->Create())
		{
			delete pSocket;
			return false;
		}

		if(!pSocket->Accept(this))
		{
			delete pSocket;
			return false;
		}

		m_AcceptSockets.push_back(pSocket);
	}

	m_nCurCreatedSockets = (int) m_AcceptSockets.size();
#endif

	return true;
}

#ifndef SC_PLATFORM_POSIX

void ServerPart::PrecrateMoreSockets(int nPrecreateCount)
{
	Log(LOG_DATE|LOG_WARN, "[%s] Create %d more sockets (cur=%d,max=%d)\n",
		m_szName, nPrecreateCount, m_nCurCreatedSockets, m_nMaxSockets);

	for(int i=0; i<nPrecreateCount; i++)
	{
		TcpSocket* pSocket = new TcpSocket();

		if(pSocket != NULL)
		{
			if(0 == pSocket->Create())
			{
				if(pSocket->Accept(this))
				{
					m_PartLock.Lock();
					{
						m_AcceptSockets.push_back(pSocket);
						++m_nCurCreatedSockets;
					}
					m_PartLock.Unlock();
					continue;
				}
				else
				{
					Log(LOG_DATE|LOG_ERROR, "[%s] Precreated socket Accept() failed\n");
				}
			}

			delete pSocket;
			break;
		}
		else
		{
			Log(LOG_DATE|LOG_FATAL, "Out of memory - new TcpSocket() failed.\n");
			break;
		}
	}
}

#endif

bool ServerPart::CreateUdpSocket()
{
	SC_ASSERT(m_pUdpSocket == NULL && IsUDP());
	
	m_pUdpSocket = new UdpSocket(m_pUdpSocketListener);
	if(!m_pUdpSocket->CreateAndBind(this))
		return false;

	Log(LOG_SYSTEM, "[%s] UDP socket listen to %s:%d\n", m_szName, m_pszListenAddress, m_nListenPort);
	
	m_pUdpSocket->PostRecv();
	m_pUdpSocket->PostRecv();
	m_pUdpSocket->PostRecv();
	m_pUdpSocket->PostRecv();

	return true;
}

int ServerPart::GetFreeSocketCount()
{
#ifndef SC_PLATFORM_POSIX
	// TODO: need locking?
	return ((LONG)m_AcceptSockets.size() - m_nConcurrentConn);
#else
	return (m_nMaxSockets - m_nConcurrentConn);//(int) m_FreeSockets.size();
#endif
}

int ServerPart::GetUseSocketCount()
{
	return m_nConcurrentConn;
}

void ServerPart::SetListenAddr(unsigned int nIpAddr)
{
	if(g_bServerStartedListening)
	{
		Log(LOG_FATAL, "[%s] SetListenAddress ignored: server already started.\n", m_szName);
	}
	else
	{
		BYTE a = (nIpAddr >> 24) & 0xFF;
		BYTE b = (nIpAddr >> 16) & 0xFF;
		BYTE c = (nIpAddr >> 8) & 0xFF;
		BYTE d = nIpAddr & 0xFF;
		StringCchPrintfA(m_szOverrideListenAddr, 16, "%d.%d.%d.%d", a, b, c, d);			
		m_pszListenAddress = m_szOverrideListenAddr;
	}
}

void ServerPart::SetListenPort(unsigned short nPort)
{
	if(g_bServerStartedListening)
	{
		if(m_nListenPort != nPort)
			Log(LOG_FATAL, "[%s] SetListenPort(%d) ignored: server already started.\n", m_szName, nPort);
	}
	else
	{
		m_nListenPort = nPort;
	}
}

// SO_REUSEADDR ensures we can reuse the port instantly
// if the process is killed while the port is in use
bool MakeSocketReuseAddr(SOCKET hSocket, const char* desc)
{
	BOOL bReuseAddr = TRUE;
	if(SOCKET_ERROR == setsockopt(hSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&bReuseAddr, sizeof(bReuseAddr)))
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "[%s] setsockopt(REUSEADDR) failed: %d %s\n",
			desc, nLastError, GetWsaErrorString(nLastError));
		return false;
	}

	return true;
}

SC_NAMESPACE_END

