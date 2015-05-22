#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

UdpSocket::UdpSocket(IUdpSocketListener* pLsnr)
{
	m_hSocket = INVALID_SOCKET;
	m_pSocketListener = pLsnr;
}

bool UdpSocket::CreateAndBind(ServerPart* pPart)
{
	int nLastError;

	m_hSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
		NULL, 0, WSA_FLAG_OVERLAPPED);

	if(m_hSocket == INVALID_SOCKET)
	{
		nLastError = WSAGetLastError();
		Log(LOG_FATAL, "UdpSocket: WSASocket failed: %d %s\n", 
			nLastError, GetWsaErrorString(nLastError));

		m_pSocketListener->OnSocketCreated(NULL, nLastError);
		return false;
	}

	// Allow LAN based broadcasts since this option is disabled by default.
	//	int value = 1;
	//	setsockopt(m_hSocket, SOL_SOCKET, SO_BROADCAST, (char *)&value, sizeof(int));

	ZeroMemory(&m_saLocalAddr, sizeof(m_saLocalAddr));	
	if(pPart->m_pszListenAddress == NULL)
	{
		m_saLocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		pPart->m_pszListenAddress = "*";
	}
	else
	{
		m_saLocalAddr.sin_addr.s_addr = inet_addr(pPart->m_pszListenAddress);
		if(m_saLocalAddr.sin_addr.s_addr == INADDR_NONE)
		{
			nLastError = WSAGetLastError();
			Log(LOG_FATAL, "[%s] Invalid UDP ListenAddress: %s\n",
				pPart->m_szName, pPart->m_pszListenAddress);

			m_pSocketListener->OnSocketCreated(NULL, nLastError);
			return false;
		}
	}

	// Bind the socket to the requested port
	m_saLocalAddr.sin_family = AF_INET;
	m_saLocalAddr.sin_port = htons(pPart->m_nListenPort);
	if(SOCKET_ERROR == bind(m_hSocket, (SOCKADDR*) &m_saLocalAddr, sizeof(m_saLocalAddr)))
	{
		nLastError = WSAGetLastError();
		Log(LOG_FATAL, "UdpSocket: bind(port=%d) failed: %d %s\n", pPart->m_nListenPort,
			nLastError, GetWsaErrorString(nLastError));

		m_pSocketListener->OnSocketCreated(NULL, nLastError);
		return false;
	}

	if(!AssociateDeviceWithCompletionPort(g_hCompPort, (HANDLE) m_hSocket, (ULONG_PTR) this))
	{
		nLastError = GetLastError();
		Log(LOG_FATAL, "UdpSocket: AssociateDeviceWithCompletionPort failed: %d\n", nLastError);

		m_pSocketListener->OnSocketCreated(NULL, nLastError);
		return false;
	}

	m_pSocketListener->OnSocketCreated(this, 0);
	return true;
}

void UdpSocket::Close()
{
	if(m_hSocket != INVALID_SOCKET)
	{
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;
	}
}

bool UdpSocket::Send(const SockAddrT* pTo, const void* pData, int len)
{
	WSABUF wb;
	DWORD dwNumberOfBytesSent;

	if((unsigned)len > g_nMaxUdpBufferSize)
	{
		Log(LOG_FATAL, "Exceeded UDP send limit %d: %d\n", g_nMaxUdpBufferSize, len);
		return false;
	}

	TUdpRwItem* pItem = g_sbpool.Get(this);
	memset(&pItem->ov, 0, sizeof(OVERLAPPED));
	pItem->ov.type = OVTYPE_UDP_WRITE;

	//memcpy(&pItem->sa, pTo, sizeof(struct sockaddr_in));
	//pItem->sa_len = sizeof(struct sockaddr_in);

	memcpy(pItem->buf, pData, len);
	wb.buf = pItem->buf;
	wb.len = len;

	if(SOCKET_ERROR == WSASendTo(m_hSocket, &wb, 1, &dwNumberOfBytesSent, 0,
		(struct sockaddr*) pTo, sizeof(struct sockaddr_in), &pItem->ov, NULL))
	{
		int nLastError = WSAGetLastError();
		if(nLastError != WSA_IO_PENDING)
		{
			Log(LOG_FATAL, "WSASendTo failed: %d (%s)\n", nLastError, GetWsaErrorString(nLastError));
			g_sbpool.Discard(pItem);
			return false;
		}
	}

	return true;
}

void UdpSocket::PostRecv()
{
	TUdpRwItem* pItem = g_sbpool.Get(this);
	memset(&pItem->ov, 0, sizeof(OVERLAPPED));
	pItem->ov.type = OVTYPE_UDP_READ;

	memcpy(&pItem->sa, &m_saLocalAddr, sizeof(struct sockaddr_in));
	pItem->sa_len = sizeof(struct sockaddr_in);

	WSABUF wb;
	wb.buf = pItem->buf;
	wb.len = g_nMaxUdpBufferSize;


	DWORD dwNumberOfBytesRecvd;
	DWORD recvFlags = 0;

	if(SOCKET_ERROR == WSARecvFrom(m_hSocket, &wb, 1,
		&dwNumberOfBytesRecvd, &recvFlags,
		(sockaddr*) &pItem->sa, &pItem->sa_len, &pItem->ov, NULL))
	{
		int nLastError = WSAGetLastError();
		if(nLastError != WSA_IO_PENDING)
		{
			Log(LOG_FATAL, "WSARecvFrom failed: %d %s", nLastError, GetWsaErrorString(nLastError));
			g_sbpool.Discard(pItem);
			return;
		}
	}
}

void UdpSocket::OnSendComplete(TUdpRwItem* pItem, DWORD cbSize)
{
	SC_ASSERT(pItem->ov.pUdpSock == this);
	g_sbpool.Discard(pItem);
}

void UdpSocket::OnRecvComplete(TUdpRwItem* pItem, DWORD cbSize)
{
	pItem->recvlen = (unsigned short) cbSize;
	SC_ASSERT(pItem->ov.pUdpSock == this);

	if(QueueUserAPC(SerializedUdpProcessor, g_hUdpWorkThread, (ULONG_PTR)pItem) == 0)
	{
		Log(LOG_FATAL, "UdpSocket.OnRecvComplete: QueueUserAPC failed\n");
		g_sbpool.Discard(pItem);
	}

	PostRecv();
}

void CALLBACK UdpSocket::SerializedUdpProcessor(ULONG_PTR dwParam)
{
	TUdpRwItem* pItem = (TUdpRwItem*) dwParam;
	pItem->ov.pUdpSock->m_pSocketListener->OnReceive((SockAddrT*)&pItem->sa, pItem->buf, pItem->recvlen);
	g_sbpool.Discard(pItem);
}

SC_NAMESPACE_END
