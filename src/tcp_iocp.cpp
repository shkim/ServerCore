#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

// If a client sends something always, ACCEPTEX_BUFFER_SIZE can be an other value than 0,
// but if not 0 and client doesn't send anything, server will wait forever.
// (Connection Timeout technique can be found in AcceptEx of MSDN.)
#define ACCEPTEX_BUFFER_SIZE		0

// if upper value is not 0, you must read something on the client's first connect notify.
//#define ACCEPTEX_BUFFER_SIZE		(MAX_RECVBUFFER_SIZE - (sizeof(sockaddr_in) + 16) * 2)

TcpSocket::TcpSocket()
{
	m_hSocket = INVALID_SOCKET;

	m_pListenSvr = NULL;
	m_pClientListenerItem = NULL;
	m_pSocketListener = NULL;

	m_pFirstPendingSend = NULL;
	m_pLastPendingSend = NULL;
	m_nPendingBufferCount = 0;
	ZeroMemory(&m_atomic, sizeof(m_atomic));

//	m_bWaitSendComplete = false;

	m_ovConn.pTcpSock = this;
	m_ovRecv.pTcpSock = this;
	m_ovRecv.type = OVTYPE_READING;

	ZeroMemory(&m_ovPostRecvReq, sizeof(m_ovPostRecvReq));
	m_ovPostRecvReq.pTcpSock = this;
	m_ovPostRecvReq.type = OVTYPE_REQ_RECVBUFF;
}

void TcpSocket::Destroy()
{
	if(m_hSocket != INVALID_SOCKET)
	{
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;

		m_SendLock.Destroy();
	}

	SC_ASSERT(m_pClientListenerItem == NULL);
	SC_ASSERT(m_pFirstPendingSend == NULL);
}

void TcpSocket::Close()
{
//	SC_ASSERT(m_atomic.Disconnected == 1);

	if(m_pClientListenerItem != NULL)
	{
		SC_ASSERT(m_pListenSvr != NULL);
		g_clipool.Discard(m_pListenSvr, m_pClientListenerItem);
		m_pClientListenerItem = NULL;
	}
}

int TcpSocket::Create()
{
	SC_ASSERT(m_hSocket == INVALID_SOCKET);

	m_hSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
		NULL, 0, WSA_FLAG_OVERLAPPED);

	if(m_hSocket == INVALID_SOCKET)
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "TcpSocket: WSASocket failed: %d %s\n", 
			nLastError, GetWsaErrorString(nLastError));

		return nLastError;
	}

	m_SendLock.Create();

// It seems OK not to set options
/*
	BOOL nodelay = TRUE;
	setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(BOOL));

	int zero = 0;
	setsockopt(m_hSocket, SOL_SOCKET, SO_SNDBUF, (char *) &zero, sizeof(zero));
	setsockopt(m_hSocket, SOL_SOCKET, SO_RCVBUF, (char *) &zero, sizeof(zero));

	// flush unsent data before socket close
	LINGER lngr;
	lngr.l_onoff  = 1;
	lngr.l_linger = 0;
	setsockopt(m_hSocket, SOL_SOCKET, SO_LINGER, (char*)&lngr, sizeof(lngr));
*/

	m_atomic.Disconnected = 1;
	SC_ASSERT(m_atomic.KickPosted == 0);
	
	return 0;
}

bool TcpSocket::Accept(ServerPart* pPart)
{
	DWORD m_dwRecvd;

	SC_ASSERT(pPart->m_hListenSocket != INVALID_SOCKET && m_hSocket != INVALID_SOCKET);

	ZeroMemory(&m_ovConn, sizeof(WSAOVERLAPPED));
	SC_ASSERT(m_ovConn.pTcpSock == this);
	m_ovConn.type = OVTYPE_ACCEPTING;

	SC_ASSERT(1 == _InterlockedIncrement(&m_atomic.ConnPostCount));
	if(FALSE == g_lpfnAcceptEx(pPart->m_hListenSocket, m_hSocket,
		m_recvbuff, ACCEPTEX_BUFFER_SIZE,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&m_dwRecvd, &m_ovConn))
	{
		int nLastError = WSAGetLastError();

		if(WSA_IO_PENDING != nLastError)
		{
			Log(LOG_ERROR, "AcceptEx failed: %d, %s\n", nLastError, GetWsaErrorString(nLastError));
			SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
			return false;
		}
	}

	if(!AssociateDeviceWithCompletionPort(g_hCompPort, (HANDLE) m_hSocket, (ULONG_PTR) this))
	{
		Log(LOG_ERROR, "TcpSocket::Accept - AssociateDeviceWithCompletionPort failed\n");
		return false;
	}

	m_pListenSvr = pPart;
	return true;
}

// Called after the successful Accept.
void TcpSocket::AfterAccept()
{
	bool bRecvPosted;
	SOCKADDR* pLocal;
	SOCKADDR* pRemote;
	int nLocalLen=0, nRemoteLen=0;

	SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
	SC_ASSERT(m_atomic.RecvPostCount == 0 && m_atomic.SendPostCount == 0 && m_atomic.KickPosted == 0);

	m_atomic.Disconnected = 0;
	//m_atomic.KickPosted = 0;
	m_atomic.ConnReset = 1;		// blocks OnDisconnect called

	g_lpfnGetAcceptExSockaddrs(m_recvbuff, ACCEPTEX_BUFFER_SIZE,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		(SOCKADDR**) &pLocal, &nLocalLen,
		(SOCKADDR**) &pRemote, &nRemoteLen);

	SC_VERIFY(1 == _InterlockedIncrement(&m_atomic.RefCount));
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);

	SC_ASSERT(nRemoteLen <= sizeof(m_saRemoteAddr) && nLocalLen <= sizeof(m_saLocalAddr));
	memcpy(&m_saRemoteAddr, pRemote, nRemoteLen);
	memcpy(&m_saLocalAddr, pLocal, nLocalLen);
	StringCchCopyA(m_szIpAddress, sizeof(m_szIpAddress), inet_ntoa(m_saRemoteAddr.sin_addr));

	int nCurConns = _InterlockedIncrement(&m_pListenSvr->m_nConcurrentConn);
	Log(LOG_DATE|LOG_SYSTEM, "Connected from: %s (Con=%d)\n", m_szIpAddress, nCurConns);
	if (nCurConns > m_pListenSvr->m_nMaxSockets)
	{
		Log(LOG_WARN, "[%s] Exceeded MaxSockets limit %d (kick)\n",
			m_pListenSvr->m_szName, m_pListenSvr->m_nMaxSockets);

		Kick();
		return;
	}

	// initialize members for socket
	m_ovConn.type = OVTYPE_UNKNOWN;
	m_atomic.PendingBytesToSend = 0;
	m_atomic.ProcessingRecvReq = 0;
	m_bCallOnSendBufferEmpty = false;

	// update socket info
	if(SOCKET_ERROR == setsockopt(m_hSocket, SOL_SOCKET, 
		SO_UPDATE_ACCEPT_CONTEXT, (char*)&(m_pListenSvr->m_hListenSocket), sizeof(SOCKET)))
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "setsockopt SO_UPDATE_ACCEPT_CONTEXT failed: %d %s\n",
			nLastError, GetWsaErrorString(nLastError));

		Kick();
		return;
	}

	_InterlockedIncrement(&m_atomic.RefCount);
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);

	SC_ASSERT(m_pSocketListener == NULL && m_pClientListenerItem == NULL);
	m_pClientListenerItem = g_clipool.Get(m_pListenSvr);
	m_pSocketListener = static_cast<ITcpSocketListener*>(m_pClientListenerItem->pListener);
	SetSendFilter(NULL);

	m_atomic.ConnReset = 0;

	// <1> for very fast connect/disconnect case:
	// (can be disconnected before below PostRecv() called)
	_InterlockedIncrement(&m_atomic.RefCount);

	if(m_pSocketListener->OnConnect(this, NULL))
	{
		// post initial receive
		WSABUF wb;

		wb.buf = m_recvbuff;
		wb.len = sizeof(m_recvbuff);
		m_nRecvBuffLen = 0;
		m_nRecvBuffStartPos = 0;

		bRecvPosted = PostRecv(&wb);
	}
	else
	{
		bRecvPosted = false;
	}

	// decrement the above increment <1>
	_InterlockedDecrement(&m_atomic.RefCount);

	if(false == bRecvPosted)
	{
		_InterlockedDecrement(&m_atomic.RefCount);
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
		Kick();
	}
	else
	{
		if ((nCurConns + m_pListenSvr->m_nPrecreateSockets) > m_pListenSvr->m_nCurCreatedSockets)
		{
			int pccnt = m_pListenSvr->m_nPrecreateSockets / g_core.GetNetworkThreadCount();
			if (pccnt <= 1)
				pccnt = 2;

			m_pListenSvr->PrecrateMoreSockets(pccnt);
		}		
	}
}

bool TcpSocket::Connect(ITcpSocketListener* pLsnr, const char* pszServerAddr, unsigned short nRemotePort, unsigned short nLocalPort)
{
	int ret;

	SC_ASSERT(m_hSocket != INVALID_SOCKET);
	SC_ASSERT(!m_pListenSvr && !m_pSocketListener && !m_pClientListenerItem);
	SC_ASSERT(m_atomic.Disconnected && !m_atomic.KickPosted);

	// save target server address
	StringCchCopyA(m_szIpAddress, sizeof(m_szIpAddress), pszServerAddr);

	// Bind the listening socket to the local IP address
//	hostent* thisHost = gethostbyname("");
//	char* ipaddr = inet_ntoa (*(struct in_addr *)*thisHost->h_addr_list);


	m_saLocalAddr.sin_family = AF_INET;
	m_saLocalAddr.sin_addr.s_addr = NULL;
	m_saLocalAddr.sin_port = htons(nLocalPort);

	if(SOCKET_ERROR == bind(m_hSocket, (SOCKADDR*) &m_saLocalAddr, sizeof(m_saLocalAddr)))
	{
		int nLastError = WSAGetLastError();
		Log(LOG_FATAL, "TcpSocket.Connect: bind() failed: %d %s\n",
			nLastError, GetWsaErrorString(nLastError));
		
		pLsnr->OnConnect(NULL, nLastError);
		return false;
	}

	ZeroMemory(&m_saRemoteAddr, sizeof(m_saRemoteAddr));

	m_saRemoteAddr.sin_addr.s_addr = inet_addr(pszServerAddr);
	if(m_saRemoteAddr.sin_addr.s_addr == INADDR_NONE)
	{
		// resolve hostname
		LPHOSTENT lphost = gethostbyname(pszServerAddr);

		if(lphost == NULL)
		{
			Log(LOG_FATAL, "DNS Lookup (gethostbyname) failed: %s\n", pszServerAddr);
			pLsnr->OnConnect(NULL, WSAGetLastError());
			return false;
		}

		m_saRemoteAddr.sin_addr.s_addr = ((LPIN_ADDR)lphost->h_addr)->s_addr;
	}

	m_saRemoteAddr.sin_family = AF_INET;
	m_saRemoteAddr.sin_port = htons(nRemotePort);

	ZeroMemory(&m_ovConn, sizeof(WSAOVERLAPPED));
	m_ovConn.type = OVTYPE_CONNECTING;
	SC_ASSERT(m_ovConn.pTcpSock == this);

	if(!AssociateDeviceWithCompletionPort(g_hCompPort, (HANDLE) m_hSocket, (ULONG_PTR) this))
	{
		Log(LOG_FATAL, "TcpSocket::Connect - AssociateDeviceWithCompletionPort failed\n");
		pLsnr->OnConnect(NULL, WSAGetLastError());
		return false;
	}

	m_pSocketListener = pLsnr;
	SetSendFilter(NULL);

	SC_ASSERT(_InterlockedIncrement(&m_atomic.ConnPostCount) == 1);
	if(FALSE == g_lpfnConnectEx(m_hSocket, (SOCKADDR *) &m_saRemoteAddr, sizeof(m_saRemoteAddr),
		NULL, 0, NULL, &m_ovConn))
	{
		int nLastError = WSAGetLastError();
		if(WSA_IO_PENDING != nLastError)
		{
			Log(LOG_FATAL, "ConnectEx(%s:%d-%d) failed: %d (%s)\n",
				pszServerAddr, nRemotePort, nLocalPort, 
				nLastError, GetWsaErrorString(nLastError));

			m_pSocketListener = NULL;
			SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
			
			pLsnr->OnConnect(NULL, nLastError);
			return false;
		}
	}

	ret = setsockopt(m_hSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

	return true;
}

void TcpSocket::AfterConnect()
{
	bool bRecvPosted;

	SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
	SC_ASSERT(m_pSocketListener != NULL);

	int len = sizeof(m_saLocalAddr);
	getsockname(m_hSocket, (struct sockaddr*) &m_saLocalAddr, &len);

	SC_ASSERT(m_atomic.KickPosted == 0);
	m_atomic.KickPosted = 0;
	m_atomic.Disconnected = 0;
	m_atomic.ConnReset = 0;
	m_atomic.PendingBytesToSend = 0;
	m_atomic.ProcessingRecvReq = 0;
	m_bCallOnSendBufferEmpty = false;

	_InterlockedIncrement(&m_atomic.RefCount);
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);

	_InterlockedIncrement(&m_atomic.RefCount);
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	if(m_pSocketListener->OnConnect(this, 0))
	{
		// post initial receive
		WSABUF wb;

		wb.buf = m_recvbuff;
		wb.len = sizeof(m_recvbuff);
		m_nRecvBuffLen = 0;
		m_nRecvBuffStartPos = 0;

		bRecvPosted = PostRecv(&wb);
	}
	else
	{
		bRecvPosted = false;
	}

	if(!bRecvPosted)
	{
		_InterlockedDecrement(&m_atomic.RefCount);
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
		Kick();
	}
}

void TcpSocket::Disconnect()
{
	if(0 != _InterlockedCompareExchange(&m_atomic.Disconnected, 1, 0))
	{
		return;
	}

	if(0 == _InterlockedCompareExchange(&m_atomic.ConnReset, 1, 0))
	{
		m_pSocketListener->OnDisconnect();
	}

	SC_ASSERT(m_hSocket != INVALID_SOCKET);
	SC_ASSERT(m_atomic.RefCount == 0 && m_atomic.RecvPostCount == 0 && m_atomic.SendPostCount == 0);

	// discard all pending send items
	while(m_pFirstPendingSend != NULL)
	{
		TSendItem* p = m_pFirstPendingSend;
		m_pFirstPendingSend = p->pNext;
		g_sbpool.Discard(p);
	}

	m_pLastPendingSend = NULL;
	m_nPendingBufferCount = 0;

	if(m_pClientListenerItem != NULL)
	{
		SC_ASSERT(m_pListenSvr != NULL);
		g_clipool.Discard(m_pListenSvr, m_pClientListenerItem);
		m_pClientListenerItem = NULL;

		Log(LOG_DATE|LOG_SYSTEM, "%s Disconnected (Con=%d)\n", m_szIpAddress,
			_InterlockedDecrement(&m_pListenSvr->m_nConcurrentConn));
	}
	else
	{
		// connect socket
	}
		
	m_pSocketListener = NULL;
	m_atomic.KickPosted = 0;

	if(m_atomic.SocketClosed)
	{
		m_atomic.SocketClosed = 0;
		SC_ASSERT(_InterlockedIncrement(&m_atomic.ConnPostCount) == 1);
		m_ovConn.type = OVTYPE_DISCONNECTING;
		AfterDisconnect();
	}
	else
	{
		ZeroMemory(&m_ovConn, sizeof(WSAOVERLAPPED));
		m_ovConn.type = OVTYPE_DISCONNECTING;
		SC_ASSERT(m_ovConn.pTcpSock == this);

		SC_ASSERT(_InterlockedIncrement(&m_atomic.ConnPostCount) == 1);
		// On Win2000, there's no DisconnectEx API, so use TransmitFile rather.
		//BOOL bRet = g_lpfnTransmitFile(m_hSocket, NULL,  0,0, &m_ovConn, NULL, TF_REUSE_SOCKET);
		BOOL bRet = g_lpfnDisconnectEx(m_hSocket, &m_ovConn, TF_REUSE_SOCKET, 0);

		if(bRet == FALSE)
		{
			int nLastError = WSAGetLastError();
			if(WSA_IO_PENDING != nLastError)
			{
				Log(LOG_ERROR, "DisconnectEx: failed, error=%d (%s)\n",
					nLastError, GetWsaErrorString(nLastError));

				SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
			}
		}
	}
}

// Called after DisconnectEx() API call
void TcpSocket::AfterDisconnect()
{
	SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);

	if(m_pListenSvr != NULL)
	{
		// a socket - created by listen (or accept) - is disconnected.
		Reuse();
	}
	else
	{
		if(m_ovConn.type == OVTYPE_CONNECTING)
		{
			// Notify that connection was not established.
			SC_ASSERT(m_pSocketListener != NULL);
			m_pSocketListener->OnConnect(NULL, -1);
		}
		else
		{
			Log(LOG_DATE|LOG_DEBUG, "ConnectSocket '%s' disconnected.\n", m_szIpAddress);
		}

		m_pSocketListener = NULL;
		SC_ASSERT(m_nPendingBufferCount == 0);
		g_core.DiscardConnectSocket(this);
	}
}

// Make the state listening, again
void TcpSocket::Reuse()
{
	DWORD m_dwRecvd;

	SC_ASSERT(m_pSocketListener == NULL && m_pListenSvr != NULL);

	ZeroMemory(&m_ovConn, sizeof(WSAOVERLAPPED));
	m_ovConn.type = OVTYPE_ACCEPTING;
	SC_ASSERT(m_ovConn.pTcpSock == this);

	SC_ASSERT(_InterlockedIncrement(&m_atomic.ConnPostCount) == 1);
	if(FALSE == g_lpfnAcceptEx(m_pListenSvr->m_hListenSocket, m_hSocket,
		m_recvbuff, ACCEPTEX_BUFFER_SIZE,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&m_dwRecvd, &m_ovConn))
	{
		DWORD nLastError = WSAGetLastError();

		if(nLastError != WSA_IO_PENDING)
		{
			Log(LOG_FATAL, "Reuse: AcceptEx failed: %d (%s)\n",
				nLastError, GetWsaErrorString(nLastError));
		
			SC_ASSERT(m_atomic.Disconnected == 1);
			SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
		}
	}
}

void TcpSocket::AfterKick()
{
	SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
	SC_ASSERT(m_atomic.SocketClosed == 1);
//	Log(LOG_DEBUG, "AfterKick called. (Socket=%x)\n", m_hSocket);

//	Log(LOG_DEBUG, "RefCnt=%d\n", m_atomic.RefCount);
	if(0 == _InterlockedDecrement(&m_atomic.RefCount))
	{
		Disconnect();
	}
}

void TcpSocket::Kick(bool bCloseNow)
{
	if(m_atomic.Disconnected)
		return;	// Disconnected value is not 0: disconnected already

	//TRACE("Kick: RefCnt=%d, SendCnt=%d, RecvCnt=%d\n", m_atomic.RefCount, m_atomic.SendPostCount, m_atomic.RecvPostCount);
	if(bCloseNow)
	{
		if(0 == _InterlockedCompareExchange(&m_atomic.SocketClosed, 1, 0))
		{
			InterlockedExchange(&m_atomic.KickPosted, 1);
			// do not decrease RefCount for wait AfterKick()

			ZeroMemory(&m_ovConn, sizeof(WSAOVERLAPPED));
			m_ovConn.type = OVTYPE_KICKING;
			SC_ASSERT(m_ovConn.pTcpSock == this);

//			Log(LOG_DEBUG, "Disconnect socket=%x\n", m_hSocket);
			SC_ASSERT(_InterlockedIncrement(&m_atomic.ConnPostCount) == 1);
			if(g_lpfnDisconnectEx(m_hSocket, &m_ovConn, TF_REUSE_SOCKET, 0))
			{
				// DisconnectEx succeeded
				return;
			}
			else				
			{
				int nLastError = WSAGetLastError();
				if(WSA_IO_PENDING != nLastError)
				{
					Log(LOG_FATAL, "Kick.DisconnectEx: failed, error=%d (%s)\n",
						nLastError, GetWsaErrorString(nLastError));

					_InterlockedDecrement(&m_atomic.RefCount);
					SC_ASSERT(_InterlockedDecrement(&m_atomic.ConnPostCount) == 0);
				}
			}
		}
	}
	else if(0 == _InterlockedCompareExchange(&m_atomic.KickPosted, 1, 0))
	{
		// KickPosted is still 0, set 1 and decrease refcnt
		_InterlockedDecrement(&m_atomic.RefCount);
	}
	else
	{
		// kick is posted
		return;
	}

	if(0 == m_atomic.RefCount)
	{
		Disconnect();
	}
}

int TcpSocket::GetPendingBytesToSend()
{
	return m_atomic.PendingBytesToSend;
}

void TcpSocket::SetCalledOnSendBufferEmpty(bool bWantCalledOnEmpty)
{
	m_bCallOnSendBufferEmpty = bWantCalledOnEmpty;
}

void TcpSocket::RequestReceiveBuffer()
{
	SC_ASSERT(m_ovPostRecvReq.type == OVTYPE_REQ_RECVBUFF && m_ovPostRecvReq.pTcpSock == this);
	PostQueuedCompletionStatus(g_hCompPort, 0, 0, (OVERLAPPED*) &m_ovPostRecvReq);
}

void TcpSocket::Send(const void* pData, int len)
{
	bool bFlushSuccess;
	bool bSendPosted;

	if(pData == NULL)
		return;

/*
	if(pData == NULL)
	{
		m_SendLock.Lock();

		if(m_pFirstPendingSend && m_pFirstPendingSend->bSent == false)
		{
			_InterlockedIncrement(&m_atomic.RefCount);
			//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
			bFlushSuccess = FlushPendingSend();
		}
		else
		{
#ifdef _DEBUG
			if(m_pFirstPendingSend == NULL)
				Log(LOG_WARN, "WARNING: Socket->Send(NULL) called but no pending send exists.\n");
#endif
			bFlushSuccess = true;
		}

		m_SendLock.Unlock();

		if(!bFlushSuccess)
		{
			_InterlockedDecrement(&m_atomic.RefCount);
			//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
			Kick();
		}
		
		return;
	}
*/

	if(len > g_nSendBufferSize)
	{
		Log(LOG_FATAL, "Send exceeded SendBufferSize limit: %d\n", len);
		Kick();
		return;
	}

	_InterlockedIncrement(&m_atomic.RefCount);
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	if(_IsDead())
	{
		_InterlockedDecrement(&m_atomic.RefCount);
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
		return;
	}

	bSendPosted = false;
	m_SendLock.Lock();

	if(m_pLastPendingSend == NULL)
	{
		// get new one
		m_pLastPendingSend = g_sbpool.Get(this);
		m_pFirstPendingSend = m_pLastPendingSend;
	}
	else
	{
		SC_ASSERT(m_pFirstPendingSend != NULL);

		if(m_pFirstPendingSend->bSent
		&& m_pFirstPendingSend == m_pLastPendingSend)
		{
			// add new one
			m_pLastPendingSend = g_sbpool.Get(this);
			m_pFirstPendingSend->pNext = m_pLastPendingSend;
		}
	}

	SC_ASSERT(m_pLastPendingSend != NULL);
	if(m_pLastPendingSend->bSent)
	{
		Log(LOG_FATAL, "IMPOSSIBLE: Two SendBuffer sent?");
		bFlushSuccess = false;
	}
	else
	{
		if(len + m_pLastPendingSend->len <= g_nSendBufferSize)
		{
			// new packet can be appended on unused area of the last buffer
			m_pSendFilter->FilterNetworkStream((BYTE*)&m_pLastPendingSend->buf[m_pLastPendingSend->len], (const BYTE*)pData, len);
			m_pLastPendingSend->len += len;
		}
		else
		{
			// split packet, copy on unused buffer of last and new item
			TSendItem* pNewItem = g_sbpool.Get(this);
			m_pLastPendingSend->pNext = pNewItem;

			int cb1 = g_nSendBufferSize - m_pLastPendingSend->len;
			m_pSendFilter->FilterNetworkStream((BYTE*)&m_pLastPendingSend->buf[m_pLastPendingSend->len], (const BYTE*)pData, cb1);
			int cb2 = len - cb1;
			m_pSendFilter->FilterNetworkStream((BYTE*)pNewItem->buf, ((const BYTE*) pData) + cb1, cb2);

			SC_ASSERT(m_pLastPendingSend->len + cb1 == g_nSendBufferSize);
			m_pLastPendingSend->len = g_nSendBufferSize;
			pNewItem->len = cb2;
			m_pLastPendingSend = pNewItem;
		}

		if(m_nPendingBufferCount > (m_pListenSvr == NULL ? g_core.m_nMaxSendBuffersPerConn : m_pListenSvr->m_nMaxSendBuffersPerConn))
		{
			Log(LOG_ERROR, "Exceeded send buffer limit(%d): %s (cur=%d,pend=%d)\n", m_nPendingBufferCount, m_szIpAddress, len,m_atomic.PendingBytesToSend);
			bFlushSuccess = false;
		}
		else if(m_pFirstPendingSend->bSent == false)
		{		
			bSendPosted = bFlushSuccess = FlushPendingSend();
		}
		else
		{
			bFlushSuccess = true;
		}
	}

	m_SendLock.Unlock();

	if(false == bSendPosted)
	{
		// WSASend not called
		_InterlockedDecrement(&m_atomic.RefCount);
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	}
	
	if(bFlushSuccess)
	{
		_InterlockedExchangeAdd(&m_atomic.PendingBytesToSend, len);
	}
	else
	{
		Kick(true);
	}
}

#if 0

void TcpSocket::SendFile(HANDLE hFile, void* param)
{
#ifdef SC_SUPPORT_SENDFILE
	m_ovSendFile.type = OVTYPE_SENDFILE;
	m_ovSendFile.pObject = this;
	m_ovSendFile.pRecvBuffer = NULL;
	BOOL ret = g_lpfnTransmitFile(m_hSocket, hFile, 0,0, &m_ovSendFile, NULL, 0);
	// TODO
	Log(LOG_DEBUG, "TransmitFile ret=%d\n", ret);
#else
	Log(LOG_FATAL, "FATAL: This build of ServerCore does not support SendFile()\n");
	Kick();
#endif
}

bool TcpSocket::WaitSendComplete(int nMinPendingBuffers, void* param)
{
	if(m_nPendingBufferCount < nMinPendingBuffers)
	{
		// not send-buffer-full situation
		return false;
	}

#ifdef SC_SUPPORT_SENDFILE
	m_pWaitCompletionParam = param;
	_InterlockedCompareExchange(&m_atomic.WaitSendComplState, 1, 0);
#endif

	return true;
}

#endif

//////////////////////////////////////////////////////////////////////////////

bool TcpSocket::PostRecv(WSABUF* pWB)
{
	if(_IsDead())
		return false;

	DWORD dwRecvd;
	DWORD dwRecvFlag = 0;

	ZeroMemory(&m_ovRecv, sizeof(WSAOVERLAPPED));
	SC_ASSERT(m_ovRecv.type == OVTYPE_READING && m_ovRecv.pTcpSock == this);
	m_ovRecv.pRecvBuffer = pWB->buf;

	if(SOCKET_ERROR == WSARecv(m_hSocket, pWB, 1,
		&dwRecvd, &dwRecvFlag, &m_ovRecv, NULL))
	{
		int nLastError = WSAGetLastError();
		if(nLastError != WSA_IO_PENDING)
		{
			if(nLastError != WSAECONNABORTED && nLastError != WSAECONNRESET)
				Log(LOG_ERROR, "WSARecv fail: %d (%s)\n", nLastError, GetWsaErrorString(nLastError));

			return false;
		}
	}

#ifdef _DEBUG
	_InterlockedIncrement(&m_atomic.RecvPostCount);
	//TRACE("RecvPost Cnt=%d\n", m_atomic.RecvPostCount);
#endif

	m_bRecvIocpRequested = true;
	return true;
}

void TcpSocket::OnRecvComplete(char* pBuffer, DWORD cbSize)
{
#ifdef _DEBUG
	_InterlockedDecrement(&m_atomic.RecvPostCount);
#endif

	if(cbSize == 0)
	{
		if(0 == _InterlockedCompareExchange(&m_atomic.ConnReset, 1, 0))
		{
			m_pSocketListener->OnDisconnect();
		}

		_InterlockedDecrement(&m_atomic.RefCount);

		Kick();
		return;
	}

	if (m_pListenSvr != NULL)
	{
		_InterlockedExchangeAdd64(&m_pListenSvr->m_nTotalRecvBytes, cbSize);
	}

	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	if(0 == _InterlockedDecrement(&m_atomic.RefCount))
	{
		SC_ASSERT(m_atomic.KickPosted);
		Disconnect();
		return;
	}
	
	if(_IsDead())
		return;

	SC_ASSERT(cbSize <= MAX_RECVBUFFER_SIZE);
	SC_ASSERT(pBuffer >= m_recvbuff && pBuffer < (m_recvbuff + m_nRecvBuffStartPos + m_nRecvBuffLen + cbSize));
	m_nRecvBuffLen += cbSize;
	m_bRecvIocpRequested = false;

	if(_InterlockedCompareExchange(&m_atomic.ProcessingRecvReq, 1, 0) == 0)
	{
		// was not processing
		_ProcessReceiveBufferRequest();
		InterlockedExchange(&m_atomic.ProcessingRecvReq, 0);
	}
	else
	{
		// on processing
		RequestReceiveBuffer();
	}
}

void TcpSocket::OnRequestRecvBuff()
{
	if (m_nRecvBuffLen == 0)
	{
		//Log(LOG_VERBOSE, "RequestReceiveBuffer() ignored by RecvBuffLen==0\n");
		return;
	}

	if(_InterlockedCompareExchange(&m_atomic.ProcessingRecvReq, 1, 0) == 0)
	{
		_ProcessReceiveBufferRequest();
		InterlockedExchange(&m_atomic.ProcessingRecvReq, 0);
	}
}

void TcpSocket::_ProcessReceiveBufferRequest()
{	
	WSABUF wb;
	bool bDoKick;

	_InterlockedIncrement(&m_atomic.RefCount);
	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);

	SC_ASSERT(m_pSocketListener != NULL);
	UINT cbSize = m_pSocketListener->OnReceive(&m_recvbuff[m_nRecvBuffStartPos], m_nRecvBuffLen);
	
	if(m_atomic.KickPosted)
	{
		bDoKick = true;
	}
	else
	{
		if (m_bRecvIocpRequested)
		{
			// do not post iocp recv
			m_nRecvBuffStartPos += cbSize;
			m_nRecvBuffLen -= cbSize;
			return;
		}

		bDoKick = false;

		if(m_nRecvBuffLen == cbSize)
		{
			m_nRecvBuffLen = 0;
			m_nRecvBuffStartPos = 0;
			wb.buf = m_recvbuff;
			wb.len = sizeof(m_recvbuff);
		}
		else if(m_nRecvBuffLen < cbSize)
		{
			Log(LOG_FATAL, "OnReceive returned more bigger number(%d) than length(%d)\n", cbSize, m_nRecvBuffLen);
			Kick();
			bDoKick = true;
		}
		else
		{
			if(cbSize > 0)
			{
				m_nRecvBuffLen -= cbSize;
				memmove(m_recvbuff, &m_recvbuff[m_nRecvBuffStartPos + cbSize], m_nRecvBuffLen);
			}

			m_nRecvBuffStartPos = 0;
			wb.buf = &m_recvbuff[m_nRecvBuffLen];
			wb.len = sizeof(m_recvbuff) - m_nRecvBuffLen;

			if(wb.len == 0)
			{
				// can't post recv req at the moment
				return;
			}
		}
	}

	if(bDoKick)
	{
_kickR:
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
		if (_InterlockedDecrement(&m_atomic.RefCount) == 0)
		{
			Disconnect();
		}
	}
	else
	{
		if (false == PostRecv(&wb))
		{
			Kick();
			goto _kickR;
		}
	}	
}

bool TcpSocket::FlushPendingSend()
// MUST BE CALLED IN CRITICAL SECTION !!!
{
	WSABUF wb;
	static DWORD dwSent;
	int ret;

	SC_ASSERT(m_pFirstPendingSend != NULL && m_pFirstPendingSend->bSent == false);
	m_pFirstPendingSend->bSent = true;
	SC_ASSERT(m_pFirstPendingSend->ov.pSendItem == m_pFirstPendingSend);
	ZeroMemory(&m_pFirstPendingSend->ov, sizeof(WSAOVERLAPPED));

	wb.len = m_pFirstPendingSend->len;
	wb.buf = &m_pFirstPendingSend->buf[m_pFirstPendingSend->sentlen];

	if(m_atomic.KickPosted)
		return false;

	ret = WSASend(m_hSocket, &wb, 1, &dwSent, 0, &m_pFirstPendingSend->ov, NULL);

	if(ret == SOCKET_ERROR)
	{
		int nLastError = WSAGetLastError();

		if(nLastError != WSA_IO_PENDING)
		{
			if(nLastError != WSAECONNABORTED && nLastError != WSAECONNRESET)	// <-- too  frequent
				Log(LOG_ERROR, "WSASend failed: %d (%s)\n",
					nLastError, GetWsaErrorString(nLastError));

			return false;
		}
	}

#ifdef _DEBUG
	_InterlockedIncrement(&m_atomic.SendPostCount);
	//TRACE("SendPost Cnt=%d\n", m_atomic.SendPostCount);
#endif

	return true;
}

void TcpSocket::OnSendComplete(TSendItem* pItem, DWORD dwSent)
{
#ifdef _DEBUG
	_InterlockedDecrement(&m_atomic.SendPostCount);
#endif

	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	if(0 == _InterlockedDecrement(&m_atomic.RefCount))
	{
		SC_ASSERT(m_atomic.KickPosted);
		Disconnect();
		return;
	}

	if(_IsDead())
		return;

	bool bFlushSuccess = false;
	bool bSendPosted = false;
	bool bSendBufferEmpty = false;

	_InterlockedIncrement(&m_atomic.RefCount);

	int negSent = -((signed) dwSent);
	if (negSent < 0)
	{
		_InterlockedExchangeAdd(&m_atomic.PendingBytesToSend, negSent);

		if (m_pListenSvr != NULL)
		{
			_InterlockedExchangeAdd64(&m_pListenSvr->m_nTotalSentBytes, dwSent);
		}
	}

	//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	m_SendLock.Lock();

	if(pItem != m_pFirstPendingSend)
	{
		Log(LOG_FATAL, "OnSendComplete: pItem(%p) != first(%p)\n", pItem, m_pFirstPendingSend);
	}
	else if(dwSent != pItem->len)
	{
		Log(LOG_FATAL, "ERROR: WSASend incomplete, req %d but %d\n", pItem->len, dwSent);

		if(dwSent < pItem->len)
		{
			if(m_pFirstPendingSend != pItem)
			{
				Log(LOG_FATAL, "OnSendComplete: pItem(%p) != first(%p)\n", pItem, m_pFirstPendingSend);
//				bFlushSuccess = false;
			}
			else
			{
				SC_ASSERT(m_pFirstPendingSend->ov.pSendItem == pItem);
				SC_ASSERT(m_pFirstPendingSend->bSent == true);

				// resend
				m_pFirstPendingSend->len -= (unsigned short) dwSent;
				m_pFirstPendingSend->sentlen += (unsigned short) dwSent;
				m_pFirstPendingSend->bSent = false;
				bSendPosted = bFlushSuccess = FlushPendingSend();
			}
		}
//		else { bFlushSuccess = false; }
	}
	else
	{
		if(pItem->bSent == false)
		{
			Log(LOG_FATAL, "pItem(len=%d)->bSent is false but OnSendComplete called.\n", pItem->len);
		}
		else
		{	
			SC_ASSERT(m_pFirstPendingSend != NULL);

			m_pFirstPendingSend = m_pFirstPendingSend->pNext;
			if(m_pFirstPendingSend == NULL)
			{
				m_pLastPendingSend = NULL;
				bFlushSuccess = true;
				bSendBufferEmpty = true;
			}
			else
			{
				bSendPosted = bFlushSuccess = FlushPendingSend();
			}
		}

		g_sbpool.Discard(pItem);
		m_nPendingBufferCount--;
	}

	m_SendLock.Unlock();

	if(false == bSendPosted)
	{
		// WSASend not called
		_InterlockedDecrement(&m_atomic.RefCount);
		//TRACE("Line=%d, RefCnt=%d\n", __LINE__, m_atomic.RefCount);
	}
	
	if(false == bFlushSuccess)
	{
		Kick();
	}
	else if (bSendBufferEmpty && m_bCallOnSendBufferEmpty)
	{
		m_pSocketListener->OnSendBufferEmpty();
	}
}

void TcpSocket::OnSendFileComplete()
{
	Log(LOG_DEBUG, "OnSendFileComplete() called\n");
}

SC_NAMESPACE_END
