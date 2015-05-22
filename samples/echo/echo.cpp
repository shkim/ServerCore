#include "../samples.h"

class Client : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client(); }

	virtual void OnFinalDestruct()
	{
		// Since Creator() allocated the instance with "new" method, deallocate with "delete".
		delete this;
	}

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode)
	{
		Log(LOG_INFO, "ECHO: Connected from %s\n", pSocket->GetRemoteAddr());
		m_pSocket = pSocket;
		return true;
	}

	virtual void OnDisconnect()
	{
		Log(LOG_INFO, "ECHO: %s Disconnected\n", m_pSocket->GetRemoteAddr());
	}

	virtual void OnRelease()
	{
		Log(LOG_INFO, "ECHO: listener released.\n");
		m_pSocket = NULL;
	}

	virtual void OnSendBufferEmpty()
	{
	}

	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength)
	{
		// Send() will copy contents of pBuffer to the internal send buffer.
		m_pSocket->Send(pBuffer, nLength);

		// OnReceive should return the used bytes count from the begining of pBuffer.
		// In this example, all (nLength) bytes were used.
		return nLength;
	}

private:
	ITcpSocket* m_pSocket;
};

class EchoServerModule : public IServerModule
{
	// Module entry point
	virtual bool Create(IServerCore* pCore)
	{
		pCore->RegisterClientListener(Client::Creator);
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(EchoServerModule)
