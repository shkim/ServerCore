#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

// socket common for Windows and Unix

void MemcpyFilter::FilterNetworkStream(unsigned char* pDstFiltered, const unsigned char* pSrcRaw, int len)
{
	memcpy(pDstFiltered, pSrcRaw, len);
};

MemcpyFilter s_ssfMemCopy;

void TcpSocket::SetSendFilter(ISocketStreamFilter* pFilter)
{
	m_pSendFilter = (pFilter == NULL) ? &s_ssfMemCopy : pFilter;
}

//////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#define _GetIpAddr(sa)	ntohl(sa.S_un.S_addr);
#else
#define _GetIpAddr(sa)	ntohl(sa.s_addr);
#endif

const char* TcpSocket::GetRemoteAddr(unsigned int* pIpAddr, unsigned short *pPortNum)
{
	if(pPortNum != NULL)
	{
		*pPortNum = ntohs(m_saRemoteAddr.sin_port);
	}

	if(pIpAddr != NULL)
	{
		*pIpAddr = _GetIpAddr(m_saRemoteAddr.sin_addr);
	}

	return m_szIpAddress;
}

void TcpSocket::GetLocalAddr(unsigned int* pIpAddr, unsigned short *pPortNum)
{
	if(pPortNum != NULL)
	{
		*pPortNum = ntohs(m_saLocalAddr.sin_port);
	}

	if(pIpAddr != NULL)
	{
		*pIpAddr = _GetIpAddr(m_saLocalAddr.sin_addr);
	}
}

//////////////////////////////////////////////////////////////////////////////

void UdpSocket::TranslateAddr(const SockAddrT* pSrc, unsigned int *pIpAddr, unsigned short *pPortNum)
{
	if(pPortNum != NULL)
	{
		*pPortNum = ntohs( ((struct sockaddr_in*)pSrc)->sin_port );
	}

	if(pIpAddr != NULL)
	{
		//*pIpAddr = ntohl( ((struct sockaddr_in*)pSrc)->sin_addr.S_un.S_addr );
		*pIpAddr = _GetIpAddr( ((struct sockaddr_in*)pSrc)->sin_addr );
	}
}

void UdpSocket::GetLocalAddr(unsigned int* pIpAddr, unsigned short *pPortNum)
{
	TranslateAddr((const SockAddrT*)&m_saLocalAddr, pIpAddr, pPortNum);
}

void UdpSocket::MakeRemoteAddr(unsigned int nRemoteIpAddr, unsigned short nRemotePort, SockAddrT* pOut)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(nRemotePort);
	sa.sin_addr.s_addr = htonl(nRemoteIpAddr);

	SC_ASSERT(sizeof(SockAddrT) == sizeof(struct sockaddr_in));
	memcpy(pOut, &sa, sizeof(SockAddrT));
}

//////////////////////////////////////////////////////////////////////////////

SendBufferPool g_sbpool;
UINT g_nMaxUdpBufferSize = IOCP_UDP_ITEMALLOC_SIZE - sizeof(TUdpRwItem);

bool SendBufferPool::Create(int nPoolSize)
{
	SC_ASSERT(nPoolSize > 0);

	SC_ASSERT(sizeof(SockAddrT) == sizeof(struct sockaddr_in));
	{
		TUdpRwItem udp;
		char* base = (char*) &udp;
		char* off = udp.buf;
		g_nMaxUdpBufferSize = IOCP_UDP_ITEMALLOC_SIZE - (int)(off - base);
	}

	m_nTcpPoolSize = 0;
	m_nUdpPoolSize = 0;

	//	IncTcpPool(nPoolSize);

	Log(LOG_SYSTEM, _T("Preallocated %d send buffers.\n"), nPoolSize);
	return true;
}

void SendBufferPool::IncTcpPool(int nCount)
{
	int cbItem = sizeof(TSendItem) + g_nSendBufferSize;
	int nAlloc = MEMORY_ALLOCATION_ALIGNMENT + nCount 
		* ((cbItem + (MEMORY_ALLOCATION_ALIGNMENT-1)) & ~(MEMORY_ALLOCATION_ALIGNMENT-1));
	BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);

	while(nAlloc-- > 0)
	{
		TSendItem* pNewNode = (TSendItem*) pFree;
		pFree += cbItem;

#ifndef SC_PLATFORM_POSIX
		pNewNode->ov.type = OVTYPE_WRITING;
		pNewNode->ov.pSendItem = pNewNode;
#endif
		m_freeTcpItems.Push((LocklessEntryT*)pNewNode);
		_InterlockedIncrement(&m_nTcpPoolSize);
	}
}

TSendItem* SendBufferPool::Get(TcpSocket* pSender)
{
	TSendItem* pRet;

	for(;;)
	{
		pRet = (TSendItem*) m_freeTcpItems.Pop();
		if(pRet != NULL)
			break;

		IncTcpPool(64);
	}

	// initialize
	pRet->pNext = NULL;
	pRet->len = 0;
#ifndef SC_PLATFORM_POSIX
	SC_ASSERT(pRet->ov.pSendItem == pRet && pRet->ov.type == OVTYPE_WRITING);
	pRet->bSent = false;
	pRet->sentlen = 0;
	pRet->ov.pTcpSock = pSender;
#endif
	pSender->m_nPendingBufferCount++;

	return pRet;
}

void SendBufferPool::Discard(TSendItem* pItem)
{
	m_freeTcpItems.Push((LocklessEntryT*)pItem);
}

//////////////////////////////////////////////////////////////////////////////
// UDP Pool

void SendBufferPool::IncUdpPool(int nCount)
{
	int nAlloc = IOCP_UDP_ITEMALLOC_SIZE * nCount + MEMORY_ALLOCATION_ALIGNMENT;
	int cbItem = IOCP_UDP_ITEMALLOC_SIZE;
	BYTE* pFree = g_poolAllocs.Alloc(&nAlloc, &cbItem);

	while(nAlloc-- > 0)
	{
		TUdpRwItem* pNewNode = (TUdpRwItem*) pFree;
		pFree += cbItem;

#ifndef SC_PLATFORM_POSIX
		//pNewNode->ov.type = OVTYPE_UDP_READ;
		pNewNode->ov.pUdpItem = pNewNode;
#endif
		m_freeUdpItems.Push((LocklessEntryT*)pNewNode);
		_InterlockedIncrement(&m_nUdpPoolSize);
	}
}

TUdpRwItem* SendBufferPool::Get(UdpSocket* pSender)
{
	TUdpRwItem* pRet;

	for(;;)
	{
		pRet = (TUdpRwItem*) m_freeUdpItems.Pop();
		if(pRet != NULL)
			break;

		IncUdpPool(64);
	}

#ifndef SC_PLATFORM_POSIX
	SC_ASSERT(pRet->ov.pUdpItem == pRet);
	pRet->ov.pUdpSock = pSender;
	//pRet->ov.type = ?;
#endif

	return pRet;
}

void SendBufferPool::Discard(TUdpRwItem* pItem)
{
	m_freeUdpItems.Push((LocklessEntryT*)pItem);
}

//////////////////////////////////////////////////////////////////////////////

#ifndef SC_PLATFORM_POSIX

unsigned int __stdcall TcpSocket::WorkerThreadProc(void*)
{
	DWORD dwNumberOfBytes;
	ULONG_PTR nCompletionKey;
	OVERLAPPED_EXT* pOV;
	DWORD dwLastError;
	BOOL bOK;

	SetMyThreadIndex();
	//	_InterlockedIncrement(&g_core.m_nThreadsCurrent);
	//	_InterlockedIncrement(&g_core.m_nThreadsBusy);

	Log(LOG_DATE|LOG_SYSTEM, "Entered NetworkThread.\n");
	
	__try {
	for(;;)
	{
		//		_InterlockedDecrement(&g_core.m_nThreadsBusy);

		bOK = GetQueuedCompletionStatus(g_hCompPort,
			&dwNumberOfBytes, &nCompletionKey, (LPOVERLAPPED*) &pOV, INFINITE);

		//		printf("GQCS: ok=%d nb=%d, ck=%x, ov=%p,%p, t=%d\n", bOK, dwNumberOfBytes, nCompletionKey, pOV,
		//			pOV == NULL ? 0 : pOV->pObject, pOV == NULL ? 0 : pOV->type);

		//		nThreadsBusy = _InterlockedIncrement(&g_core.m_nThreadsBusy);

		if(FALSE == bOK)
		{
			dwLastError = GetLastError();

			if(pOV != NULL)
			{
				if(dwNumberOfBytes == 0)
				{
					// disconnected
#ifdef _DEBUG
					if(!_CrtIsValidPointer(pOV, sizeof(OVERLAPPED_EXT), TRUE))
					{
						Log(LOG_ERROR, "Disconnected but pOV is invalid.\n");
						continue;
					}
#endif
					//					Log(LOG_DEBUG, "xx got socket close: type=%d\n", pOV->type);

					switch(pOV->type)
					{
					case OVTYPE_READING:
						pOV->pTcpSock->OnRecvComplete(pOV->pRecvBuffer, 0);
						break;

					case OVTYPE_WRITING:
						_InterlockedDecrement(&pOV->pTcpSock->m_atomic.RefCount);
						//TRACE("Line=%d, RefCnt=%d\n", __LINE__, pOV->pObject->m_atomic.RefCount);
						pOV->pTcpSock->Kick();
						break;

					case OVTYPE_CONNECTING:
						// couldn't connect to server
						pOV->pTcpSock->AfterDisconnect();
						break;

					case OVTYPE_ACCEPTING:
					case OVTYPE_DISCONNECTING:
						if (pOV->pTcpSock->m_pListenSvr != NULL)
						{
							pOV->pTcpSock->m_pListenSvr->DiscardMalfunctionSocket(pOV->pTcpSock);
						}
						else
						{
							Log(LOG_FATAL, "Non-accept socket QGCS failed (type=%d)\n", pOV->type);							
						}
						break;

					case OVTYPE_KICKING:
						Log(LOG_ERROR, "Kick.Disconnect failed\n");
						pOV->pTcpSock->AfterKick();
						break;

					case OVTYPE_UDP_READ:
						Log(LOG_ERROR, "UDP.Read failed\n");
						break;
					case OVTYPE_UDP_WRITE:
						Log(LOG_ERROR, "UDP.Write failed\n");
						break;
						
					default:
						Log(LOG_FATAL, "Unknown object type %d (GQCS dwTransfer=0)\n", pOV->type);
					}
				}
				else
				{
					Log(LOG_FATAL, "GetQueuedCompletionStatus dequeued failed I/O: %d\n", dwLastError);
					break;
				}
			}
			else if(dwLastError == WAIT_TIMEOUT)
			{
				// timed out
				SC_ASSERT(!"IOCP Timeout is impossible");
			}
			else
			{
				Log(LOG_FATAL, "Bad call to GetQueuedCompletionStatus, reason: %d\n", dwLastError);
			}

			continue;
		}

		if(pOV != NULL)
		{			
			switch(pOV->type)
			{
			case OVTYPE_READING:
				pOV->pTcpSock->OnRecvComplete(pOV->pRecvBuffer, dwNumberOfBytes);
				break;

			case OVTYPE_WRITING:
				pOV->pTcpSock->OnSendComplete(pOV->pSendItem, dwNumberOfBytes);
				break;

			case OVTYPE_ACCEPTING:
				pOV->pTcpSock->AfterAccept();
				break;

			case OVTYPE_CONNECTING:
				pOV->pTcpSock->AfterConnect();
				break;

			case OVTYPE_DISCONNECTING:
				pOV->pTcpSock->AfterDisconnect();
				break;

			case OVTYPE_KICKING:
				pOV->pTcpSock->AfterKick();
				break;

			case OVTYPE_REQ_RECVBUFF:
				pOV->pTcpSock->OnRequestRecvBuff();
				break;

			case OVTYPE_SENDFILE:
				pOV->pTcpSock->OnSendFileComplete();
				break;

			case OVTYPE_UDP_READ:
				pOV->pUdpSock->OnRecvComplete(pOV->pUdpItem, dwNumberOfBytes);
				break;

			case OVTYPE_UDP_WRITE:
				pOV->pUdpSock->OnSendComplete(pOV->pUdpItem, dwNumberOfBytes);
				break;

			default:
				Log(LOG_FATAL, "unknown ov type: %d\n", pOV->type);
			}
		}
		else
		{
			if(nCompletionKey == CK_TERMINATE_THREAD)
			{
				//printf("got CK_TERMINATE_THREAD\n");
				break;
			}
			else if(nCompletionKey == CK_HELLO)
			{
				Log(LOG_DEBUG, "Thread alive (%d)\n", GetCurrentThreadId());
			}
			else if(nCompletionKey != 0)
			{
				//TcpSocket* pObj = (TcpSocket*) nCompletionKey;
				Log(LOG_DEBUG, "ov->cli=%p\n", pOV->pTcpSock);
			}
			else
			{
				Log(LOG_FATAL, "-_-\n");
			}
		}
	} // for(;;)
	} __except (CreateCrashDump(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {}

	//	_InterlockedDecrement(&g_core.m_nThreadsBusy);
	//	_InterlockedDecrement(&g_core.m_nThreadsCurrent);
	//_quitIOCPT:
	Log(LOG_SYSTEM, "Quit from NetworkThread.\n");

	Hazard_OnThreadExit();

	return 0;
}

#endif

SC_NAMESPACE_END
