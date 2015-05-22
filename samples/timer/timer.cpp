/*
	ServerCore Sample, Timer

	With timer, the server can periodically send data to the client
	regardless of the client's request.
*/
#include "timer.h"

IServerCore* g_pCore;

class TimerServerModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		g_pCore = pCore;
		pCore->RegisterClientListener(Client::Creator);
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(TimerServerModule)

//////////////////////////////////////////////////////////////////////////////

#ifndef _WIN32
DWORD GetTickCount()                                                                                                         
{                                                                                                                            
	struct timeval curr;                                                                                                     
	gettimeofday(&curr, NULL);                                                                                               
	return (curr.tv_sec * 1000) + (curr.tv_usec / 1000);                                                                     
}
#endif

bool Client::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	m_pSocket = pSocket;
	m_dwPrevTick = GetTickCount();
	m_nTimerCount = 0;
	g_pCore->SetTimer(this, rand(), 1000, true);
	return true;
}

void Client::OnDisconnect()
{
	g_pCore->ClearTimer(this);
}

void Client::OnRelease()
{
	m_pSocket = NULL;
}

void Client::OnFinalDestruct()
{
	delete this;
}

unsigned int Client::OnReceive(char* pBuffer, unsigned int nLength)
{
	m_pSocket->Send(pBuffer, nLength);
	return nLength;
}

void Client::OnTimer(int nTimerID)
{
	char buff[256];

	DWORD curTick = GetTickCount();
	StringCchPrintfA(buff, 256, "OnTimer(id=%d): %d ticks #%d\r\n", 
		nTimerID, curTick - m_dwPrevTick, ++m_nTimerCount);
	m_dwPrevTick = curTick;

	SC_ASSERT(m_pSocket);
	m_pSocket->Send(buff, (int) strlen(buff)+1);
}
