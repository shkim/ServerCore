#include "stdafx.h"
#include "scimpl.h"

#ifdef _WIN32
#define SHUT_RDWR	SD_BOTH
#endif

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

	m_SendLock.Create();
}

TcpSocket::~TcpSocket()
{
	ASSERT(m_hSocket == INVALID_SOCKET);
	m_SendLock.Destroy();
}

int TcpSocket::Create()
{
	ASSERT(m_hSocket == INVALID_SOCKET);
	m_atomic.Disconnected = 1;
	return 0;
}

void TcpSocket::Destroy()
{
	if(m_hSocket != INVALID_SOCKET)
	{
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;
	}

	ASSERT(m_pClientListenerItem == NULL);
}

void TcpSocket::Close()
{
	ASSERT(m_atomic.Disconnected == 1);
//	Log(LOG_INFO, "TcpSocket::Close(%x) %d\n", m_hSocket, m_atomic.Disconnected);

	if(m_pClientListenerItem != NULL)
	{
		ASSERT(m_pListenSvr != NULL);
		g_clipool.Discard(m_pListenSvr, m_pClientListenerItem);
		m_pClientListenerItem = NULL;
	}

	m_atomic.KickPosted = 0;
	m_atomic.SocketClosed = 0;
	m_atomic.ConnReset = 0;
}

bool TcpSocket::Accept(ServerPart* pPart, SOCKET hSocket, struct sockaddr_in& addr)
{
	ASSERT(m_atomic.KickPosted == 0 && m_atomic.Disconnected == 1);
	ASSERT(m_pLastPendingSend == NULL && m_pFirstPendingSend == NULL && m_nPendingBufferCount == 0);

	m_pListenSvr = pPart;
	m_hSocket = hSocket;
	m_bSendable = true;

	m_saRemoteAddr = addr;
	StringCchCopyA(m_szIpAddress, 16, inet_ntoa(addr.sin_addr));

	socklen_t len = sizeof(m_saLocalAddr);
	getsockname(hSocket, (struct sockaddr*) &m_saLocalAddr, &len);

	Log(LOG_DATE|LOG_SYSTEM, "Connected from: %s (Con=%d)\n", m_szIpAddress,
		_InterlockedIncrement(&m_pListenSvr->m_nConcurrentConn));

//	ZeroMemory(&m_atomic, sizeof(m_atomic));
	ASSERT(m_pSocketListener == NULL);
	m_nRecvBuffLen = 0;
	m_atomic.Disconnected = 0;

	VERIFY(1 == _InterlockedIncrement(&m_atomic.RefCount));

	if(!MakeSocketNonBlock(hSocket, m_szIpAddress))
	{
		return false;
	}

	SetSendFilter(NULL);

	_InterlockedIncrement(&m_atomic.RefCount);
	m_pClientListenerItem = g_clipool.Get(m_pListenSvr);
	m_pSocketListener = static_cast<ITcpSocketListener*>(m_pClientListenerItem->pListener);
	bool bSuccess = m_pSocketListener->OnConnect(this, NULL);
	_InterlockedDecrement(&m_atomic.RefCount);

	return bSuccess;
}

bool TcpSocket::Connect(ITcpSocketListener* pLsnr, const char* pszServerAddr, int nRemotePort, int nLocalPort)
{
	struct sockaddr_in addr;
	struct sockaddr_in sa;
	int ret;

	ASSERT(m_hSocket == INVALID_SOCKET);
	ASSERT(!m_pListenSvr && !m_pSocketListener && !m_pClientListenerItem);
	ASSERT(m_atomic.Disconnected && !m_atomic.KickPosted);

	m_hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(!MakeSocketNonBlock(m_hSocket, pszServerAddr))
		return false;

//	ZeroMemory(&m_atomic, sizeof(m_atomic));
	m_nRecvBuffLen = 0;

	// save target server address
	StringCchCopyA(m_szIpAddress, sizeof(m_szIpAddress), pszServerAddr);

	// Bind the listening socket to the local IP address
//	struct hostent* thisHost = gethostbyname("");
//	char* ipaddr = inet_ntoa (*(struct in_addr *)*thisHost->h_addr_list);

	//if(m_nLocalBoundPort < 0)
	{
		ZeroMemory(&sa, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = NULL;
		sa.sin_port = htons(nLocalPort);

		if(SOCKET_ERROR == bind(m_hSocket, (struct sockaddr*) &sa, sizeof(sa)))
		{
			int nLastError = WSAGetLastError();
			Log(LOG_FATAL, _T("Socket::Connect - bind() failed: %d %s\n"),
				nLastError, GetWsaErrorString(nLastError));

			return false;
		}
	}

	ZeroMemory(&addr, sizeof(addr));
	addr.sin_addr.s_addr = inet_addr(pszServerAddr);
	if(addr.sin_addr.s_addr == INADDR_NONE)
	{
		// resolve hostname
		struct hostent* phost = gethostbyname(pszServerAddr);

		if(phost == NULL)
		{
			Log(LOG_FATAL, "DNS Lookup (gethostbyname) failed: %s\n", pszServerAddr);
			return false;
		}

		addr.sin_addr.s_addr = ((struct in_addr*)phost->h_addr)->s_addr;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(nRemotePort);


	//m_nLocalBoundPort = nLocalPort;
	m_pSocketListener = pLsnr;
	VERIFY(1 == _InterlockedIncrement(&m_atomic.RefCount));

	SetSendFilter(NULL);

	ret = connect(m_hSocket, (sockaddr*) &addr, sizeof(addr));
	if(ret < 0)
	{
		int nLastError = WSAGetLastError();
		if(nLastError != EINPROGRESS
#ifdef _WIN32
		&& nLastError != WSA_IO_PENDING
#endif
		&& nLastError != EWOULDBLOCK)
		{
			m_pSocketListener = NULL;
			VERIFY(0 == _InterlockedDecrement(&m_atomic.RefCount));

			Log(LOG_FATAL, "connect(%s:%d-%d) failed: %d (%s)\n",
				pszServerAddr, nRemotePort, nLocalPort,
				nLastError, GetWsaErrorString(nLastError));

			m_pSocketListener = NULL;
			pLsnr->OnConnect(NULL, nLastError);			
			return false;
		}
	}

	m_saRemoteAddr = addr;
	socklen_t len = sizeof(m_saLocalAddr);
	getsockname(m_hSocket, (struct sockaddr*) &m_saLocalAddr, &len);

	m_atomic.Disconnected = 0;

	AssociateWithPoller(m_hSocket, this);

	if(ret == 0)
	{
		// immediately connected
		if(!m_pSocketListener->OnConnect(this, 0))
			Kick();
	}

	return true;
}

void TcpSocket::IO_OnException()
{
	// socket closed
	Kick();
}

// this method is executed on main thread. (NOT ON IO-WORKER THREAD)
void TcpSocket::IO_OnReadable()
{
	if(m_atomic.KickPosted)
		return;

	//if(0 != InterlockedCompareExchange(&m_atomic.IsProcessing, 1, 0))
	//if(!AtomicCas(&m_atomic.LockRecvBuffer, 0, 1))
	if(0 != _InterlockedCompareExchange(&m_atomic.LockRecvBuffer, 1, 0))
	{
		// locking failed: worker thread is processing it.
		// TODO: add to pending io list (main-thread-only-touchable)
		_InterlockedCompareExchange(&m_atomic.PendingRead, 1, 0);
		return;
	}

	int cbCanRecv = MAX_RECVBUFFER_SIZE - m_nRecvBuffLen;
	if(cbCanRecv <= 0)
	{
		// Buffer full, lazy input processor?
		ASSERT(!"RecvBuffer full");
	}
	else
	{
		int cbSize = recv(m_hSocket, &m_recvbuff[m_nRecvBuffLen], cbCanRecv, 0);
		if(cbSize > 0)
		{
			m_nRecvBuffLen += cbSize;
			ASSERT(m_nRecvBuffLen <= MAX_RECVBUFFER_SIZE);

			_InterlockedIncrement(&m_atomic.RefCount);
			g_cioq.Put(CIO_READ, this);
		}
		else if(cbSize < 0)
		{
			int nErrorCode = WSAGetLastError();
			if(nErrorCode != EAGAIN && nErrorCode != EWOULDBLOCK)
			{
				if(nErrorCode != ECONNRESET)
					Log(LOG_FATAL, "Socket(%d).recv failed error=%d, %s\n", m_hSocket, nErrorCode, GetWsaErrorString(nErrorCode));

				goto _socket_error;
			}
		}
		else
		{
			ASSERT(cbSize == 0);
_socket_error:
			Kick();
/*
			//if(0 == InterlockedCompareExchange(&m_atomic.ConnReset, 1, 0))
			if(AtomicCas(&m_atomic.ConnReset, 0, 1))
			{
				m_pRemoteListener->OnDisconnect();
			}
*/
		}
	}

	//VERIFY(AtomicCas(&m_atomic.LockRecvBuffer, 1, 0));
	VERIFY(1 == _InterlockedCompareExchange(&m_atomic.LockRecvBuffer, 0, 1));
}

void TcpSocket::OnRecvComplete()
{
	unsigned int cbSize;

	// make other thread not to disconnect (m_pSocketListener should not be null)
	_InterlockedIncrement(&m_atomic.RefCount);	// <1>

	if(1 == _InterlockedDecrement(&m_atomic.RefCount))
	{
		// kicked before
		VERIFY(0 == _InterlockedDecrement(&m_atomic.RefCount));	// <1>
		ASSERT(m_atomic.KickPosted);
		Disconnect();
		return;
	}

	if(0 != _InterlockedCompareExchange(&m_atomic.LockRecvBuffer, 1, 0))
	{
		// locking failed: main thread may read socket again
		_InterlockedDecrement(&m_atomic.RefCount);	// <1>
		return;
	}

	ASSERT(m_pSocketListener != NULL);

_processAgain:
	cbSize = m_pSocketListener->OnReceive(m_recvbuff, m_nRecvBuffLen);

	if(m_atomic.KickPosted)
	{
		Kick();
	}
	else
	{
		if(m_nRecvBuffLen == cbSize)
		{
			m_nRecvBuffLen = 0;
		}
		else if(cbSize > 0)
		{
			m_nRecvBuffLen -= cbSize;
			memmove(m_recvbuff, &m_recvbuff[cbSize], m_nRecvBuffLen);
		}
	}

	if(_InterlockedCompareExchange(&m_atomic.PendingRead, 0, 1) != 0)
	{
		// TODO: Don't repeat recv code here
		int cbCanRecv = MAX_RECVBUFFER_SIZE - m_nRecvBuffLen;
		int ret = recv(m_hSocket, &m_recvbuff[m_nRecvBuffLen], cbCanRecv, 0);
		if(ret > 0)
		{
			m_nRecvBuffLen += ret;
			ASSERT(m_nRecvBuffLen <= MAX_RECVBUFFER_SIZE);
			goto _processAgain;
		}
		else if(ret < 0)
		{
			int nErrorCode = WSAGetLastError();
			if(nErrorCode != EAGAIN && nErrorCode != EWOULDBLOCK)
			{
				Log(LOG_FATAL, "Socket(%x).recv failed error=%d, %s\n", m_hSocket, nErrorCode, GetWsaErrorString(nErrorCode));
				Kick();
			}
		}
		else
		{
			Kick();
		}
	}

	VERIFY(1 == _InterlockedCompareExchange(&m_atomic.LockRecvBuffer, 0, 1));

	if(0 == _InterlockedDecrement(&m_atomic.RefCount))	// <1>
	{
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

	ASSERT(m_hSocket != INVALID_SOCKET);
	ASSERT(m_atomic.RefCount == 0);

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
		ASSERT(m_pListenSvr != NULL);
		g_clipool.Discard(m_pListenSvr, m_pClientListenerItem);
		m_pClientListenerItem = NULL;

		Log(LOG_DATE|LOG_SYSTEM, "%s Disconnected. (Con=%d)\n", m_szIpAddress,
			_InterlockedDecrement(&m_pListenSvr->m_nConcurrentConn));
	}

	m_pSocketListener = NULL;
	
	if(0 == _InterlockedCompareExchange(&m_atomic.SocketClosed, 1, 0))
	{
		shutdown(m_hSocket, SHUT_RDWR);
	}

	UnassociateWithPoller(m_hSocket, this);
	g_cioq.Put(CIO_DISCONNECTED, this);
}

void TcpSocket::OnDisconnectComplete()
{
	Close();

	if(m_pListenSvr != NULL)
	{
		m_pListenSvr->OnTcpSocketClosed(this);
	}
	else
	{
		// connect socket
		g_core.DiscardConnectSocket(this);
	}
}

void TcpSocket::Kick(bool bCloseNow)
{
	if(m_atomic.Disconnected)
		return;

	if(bCloseNow)
	{
		if(0 == _InterlockedCompareExchange(&m_atomic.SocketClosed, 1, 0))
		{
			m_atomic.KickPosted = 1;
			_InterlockedDecrement(&m_atomic.RefCount);
			shutdown(m_hSocket, SHUT_RDWR);
		}
	}
	else if(0 == _InterlockedCompareExchange(&m_atomic.KickPosted, 1, 0))
	{
		// KickPosted is still 0, set 1 and decrease refcount
		_InterlockedDecrement(&m_atomic.RefCount);
	}

	if(0 == m_atomic.RefCount)
	{
		Disconnect();
	}
}

void TcpSocket::OnCanWrite()
{
	if(0 == _InterlockedDecrement(&m_atomic.RefCount))
	{
		ASSERT(m_atomic.KickPosted);
		Disconnect();
		return;
	}
	
	m_bSendable = true;

	if(m_nPendingBufferCount > 0)
	{
		if(!FlushPendingSend())
		{
			Kick();
			return;
		}
	}

	if(m_bSendable)
		SetSendableInterestWithPoller(m_hSocket, this, false);
}

bool TcpSocket::FlushPendingSend()
{
	ASSERT(m_bSendable && m_nPendingBufferCount > 0 && m_pFirstPendingSend != NULL);

	bool bSuccess = true;
	m_SendLock.Lock();

	while(m_bSendable && m_pFirstPendingSend != NULL)
	{
		int sent = send(m_hSocket, m_pFirstPendingSend->buf,
			m_pFirstPendingSend->len, 0);

		if(sent < 0)
		{
			int nErrorCode = WSAGetLastError();
			if(nErrorCode == EAGAIN || nErrorCode == EWOULDBLOCK)
			{
				m_bSendable = false;
			}
			else
			{
				Log(LOG_FATAL, "Send failer, error=%d(%s)\n", nErrorCode, GetWsaErrorString(nErrorCode));
				bSuccess = false;
			}

			break;
		}
		else if(sent == m_pFirstPendingSend->len)
		{
			// fully sent
			TSendItem* pNext = m_pFirstPendingSend->pNext;
			g_sbpool.Discard(m_pFirstPendingSend);
			m_nPendingBufferCount--;

			m_pFirstPendingSend = pNext;
			if(pNext == NULL)
			{
				m_pLastPendingSend = NULL;
				break;
			}
		}
		else
		{
			ASSERT(sent > 0);
			m_pFirstPendingSend->len -= sent;
			memmove(m_pFirstPendingSend->buf, &m_pFirstPendingSend->buf[sent],
				m_pFirstPendingSend->len);
			m_bSendable = false;
			break;
		}
	}

	m_SendLock.Unlock();

	return bSuccess;
}

extern MemcpyFilter s_ssfMemCopy;

void TcpSocket::Send(const void* pData, int len)
{
	int nErrorCode;

	if(pData == NULL || m_atomic.KickPosted)
		return;

	ASSERT(len <= MAX_SENDBUFFER_SIZE);

	if(m_bSendable)
	{
		if(m_nPendingBufferCount > 0)
		{
			if(!FlushPendingSend())
			{
				Kick();
				return;
			}

			if(!m_bSendable)
				goto _pendToTail;
		}

		TSendItem* pTempBuff = NULL;

		m_SendLock.Lock();

		if(m_pSendFilter != &s_ssfMemCopy)
		{
			pTempBuff = g_sbpool.Get(this);
			m_pSendFilter->FilterNetworkStream(
				(BYTE*) pTempBuff->buf, (BYTE*) pData, len);

			int sent = send(m_hSocket, pTempBuff->buf, len, 0);

			if(sent < 0)
			{
				nErrorCode = WSAGetLastError();
				if(nErrorCode == EAGAIN || nErrorCode == EWOULDBLOCK)
				{
					m_bSendable = false;

					// add to pending list
					if(m_pLastPendingSend == NULL)
					{
						m_pFirstPendingSend = m_pLastPendingSend = pTempBuff;
						pTempBuff->len = len;
						pTempBuff = NULL;
					}
					else
					{
						int space = g_nSendBufferSize - m_pLastPendingSend->len;
						if(len <= space)
						{
							memcpy(&m_pLastPendingSend->buf[m_pLastPendingSend->len],
								pTempBuff->buf, len);
							m_pLastPendingSend->len += len;
						}
						else
						{
							m_pLastPendingSend->pNext = pTempBuff;
							pTempBuff->len = len;
							pTempBuff = NULL;
						}
					}

					len = 0;
				}
				else
				{
					len = -1;
				}
			}
			else if(sent == len)
			{
				// fully sent
				len = 0;
			}
			else
			{
				// partially sent
				len -= sent;
				ASSERT(sent > 0 && len > 0);

				if(m_pLastPendingSend == NULL)
				{
					m_pFirstPendingSend = m_pLastPendingSend = pTempBuff;
_setTail:
					memmove(pTempBuff->buf, &pTempBuff->buf[sent], len);
					pTempBuff->len = len;
					pTempBuff = NULL;
				}
				else
				{
					int space = g_nSendBufferSize - m_pLastPendingSend->len;
					if(len <= space)
					{
						memcpy(&m_pLastPendingSend->buf[m_pLastPendingSend->len],
							&pTempBuff->buf[sent], len);
						m_pLastPendingSend->len += len;
					}
					else
					{
						m_pLastPendingSend->pNext = pTempBuff;
						goto _setTail;
					}
				}

				len = 0;
			}
		}
		else
		{
			int sent = send(m_hSocket, (const char*) pData, len, 0);

			if(sent < 0)
			{
				nErrorCode = WSAGetLastError();
				if(nErrorCode == EAGAIN || nErrorCode == EWOULDBLOCK)
				{
					m_bSendable = false;
				}
				else
				{
					len = -1;
				}
			}
			else if(sent == len)
			{
				// fully sent
				len = 0;
			}
			else
			{
				pData = ( (BYTE*) pData + sent );
				len -= sent;
				ASSERT(sent > 0 && len > 0);
			}
		}

		if(pTempBuff != NULL)
			m_nPendingBufferCount--;

		m_SendLock.Unlock();

		if(pTempBuff != NULL)
			g_sbpool.Discard(pTempBuff);

		if(len < 0)
		{
			Log(LOG_FATAL, "Send failure, error=%d(%s)\n", nErrorCode, GetWsaErrorString(nErrorCode));
			Kick();
			return;
		}
		else if(len == 0)
		{
			return;
		}
	}

_pendToTail:

	m_SendLock.Lock();

	if(m_pLastPendingSend == NULL)
	{
		m_pLastPendingSend = g_sbpool.Get(this);
		m_pFirstPendingSend = m_pLastPendingSend;
	}

	if(len + m_pLastPendingSend->len <= g_nSendBufferSize)
	{
		m_pSendFilter->FilterNetworkStream(
			(BYTE*) &m_pLastPendingSend->buf[m_pLastPendingSend->len],
			(BYTE*) pData, len);

		m_pLastPendingSend->len += len;
	}
	else
	{
		// split and span
		TSendItem* pNewItem = g_sbpool.Get(this);
		m_pLastPendingSend->pNext = pNewItem;

		int cb1 = g_nSendBufferSize - m_pLastPendingSend->len;
		m_pSendFilter->FilterNetworkStream(
			(BYTE*)&m_pLastPendingSend->buf[m_pLastPendingSend->len], (BYTE*)pData, cb1);
		int cb2 = len - cb1;
		m_pSendFilter->FilterNetworkStream(
			(BYTE*)pNewItem->buf, ((BYTE*) pData) + cb1, cb2);

		ASSERT(m_pLastPendingSend->len + cb1 == g_nSendBufferSize);
		m_pLastPendingSend->len = g_nSendBufferSize;
		pNewItem->len = cb2;
		m_pLastPendingSend = pNewItem;
	}

	m_SendLock.Unlock();

	if(m_nPendingBufferCount > (m_pListenSvr == NULL ? g_core.m_nMaxSendBuffersPerConn : m_pListenSvr->m_nMaxSendBuffersPerConn))
	{
		Log(LOG_ERROR, "Exceeded send buffer limit(%d): %s\n",
			m_nPendingBufferCount, m_szIpAddress);
		Kick();
		return;
	}

	if(!m_bSendable)
		SetSendableInterestWithPoller(m_hSocket, this, true);
}


void TcpSocket::IO_OnWritable()
{
	_InterlockedIncrement(&m_atomic.RefCount);
	g_cioq.Put(CIO_WRITE, this);
}

int TcpSocket::GetPendingBytesToSend()
{
	return 0;
}

void TcpSocket::SetCalledOnSendBufferEmpty(bool bWantCalledOnEmpty)
{
}

void TcpSocket::RequestReceiveBuffer()
{
}
