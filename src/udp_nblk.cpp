#include "stdafx.h"
#include "scimpl.h"

//////////////////////////////////////////////////////////////////////////////

UdpSocket::UdpSocket(IUdpSocketListener* pLsnr)
{
	m_hSocket = INVALID_SOCKET;
	m_pSocketListener = pLsnr;
}

bool UdpSocket::CreateAndBind(ServerPart* pPart)
{
	return false;
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
	return false;
}

void UdpSocket::PostRecv()
{
}

void UdpSocket::IO_OnReadable() {}
void UdpSocket::IO_OnWritable() {}
void UdpSocket::IO_OnException() {}


//////////////////////////////////////////////////////////////////////////////

void* TcpSocket::WorkerThreadProc(void *pv)
{
	CompletedIoInfo obj;

	SetMyThreadIndex();
	Log(LOG_DATE|LOG_SYSTEM, "Entered NetworkThread.\n");

	for(;;)
	{
		g_cioq.Get(&obj);

		switch(obj.type)
		{
		case CIO_READ:
			//obj.pSocket->ProcessInput();
			obj.pSocket->OnRecvComplete();
			break;

		case CIO_WRITE:
			Log(LOG_DEBUG, "CIO_WRITE\n");
			obj.pSocket->OnCanWrite();
			break;

		case CIO_DISCONNECTED:
			obj.pSocket->OnDisconnectComplete();
			break;

		case CIO_TIMEOUT:
			if(obj.pTimerListener->Ref_Retain() > 1)
				obj.pTimerListener->OnTimer(obj.param);
			obj.pTimerListener->Ref_Release();
			break;

		case CIO_ERROR:
			Log(LOG_DEBUG, "IEQ Got Error\n");
			goto _quit;

		case CIO_CORE_SHUTDOWN:
			goto _quit;

		default:
			Log(LOG_FATAL, "IEQ Invalid ret: %d\n", obj.type);
		}
	}

_quit:
	Log(LOG_SYSTEM, "Quit from NetworkThread.\n");

	Hazard_OnThreadExit();

	return 0;
}
