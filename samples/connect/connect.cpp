#include "connect.h"

IServerCore* g_pCore;
Client cli;

class DelayedConnect : public ITimerListener
{
	// fake refcnt. before the Core calls OnTimer, it checks if ++refcnt > 1, so returns 2:
	virtual ATOMIC32 Ref_Retain() { return 2; }
	virtual void Ref_Release() {}
	virtual ATOMIC32 Ref_GetCount() { return 2; }

public:
	virtual void OnTimer(int nTimerID)
	{
		if(!g_pCore->Connect(&cli, "localhost", 23))
		{
			// false return of Connect() means 
			// there were errors in preparing the connection.
			Log(LOG_INFO, "Connect failed.\n");
		}

		// Although Connect() returns true,
		// It does not mean that the connection was established.
		// It can finally fail if, for example, the remote server is down.
	}
};

class MyModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		g_pCore = pCore;

		pCore->RegisterClientListener(Dummy::Creator);

		// at this moment, the server is not listening yet.
		// postpone the connecting for 1 second.
		static DelayedConnect dconn;
		pCore->SetTimer(&dconn, 0, 1000, false);

		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(MyModule)

//////////////////////////////////////////////////////////////////////////////

bool Client::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	if(pSocket == NULL)
	{
		Log(LOG_INFO, "Couldn't connect.\n");
		return false;
	}
	else
	{
		Log(LOG_INFO, "Connected.\n");
		m_pSocket = pSocket;
		m_pSocket->Send("Hehe", 4);
		return true;
	}
}

void Client::OnDisconnect()
{
	Log(LOG_INFO, "Disconnected.\n");
	m_pSocket = NULL;
}

unsigned int Client::OnReceive(char* pBuffer, unsigned int nLength)
{
	m_pSocket->Send(pBuffer, nLength);
	m_pSocket->Kick(true);
	return nLength;
}

//////////////////////////////////////////////////////////////////////////////

bool Dummy::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	Log(LOG_INFO, "Dummy connected.\n");
	m_pSocket = pSocket;
	return true;
}

void Dummy::OnDisconnect()
{
	Log(LOG_INFO, "Dummy disconnected.\n");
}

void Dummy::OnRelease()
{
	m_pSocket = NULL;
}

void Dummy::OnFinalDestruct()
{
	delete this;
}

unsigned int Dummy::OnReceive(char* pBuffer, unsigned int nLength)
{
	m_pSocket->Send(pBuffer, nLength);
	return nLength;
}
