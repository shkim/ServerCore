#include "../samples.h"

class Client1 : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client1(); }

	virtual void OnFinalDestruct()
	{
		delete this;
	}

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode)
	{
		Log(LOG_INFO, "ECHO1: Connected from %s\n", pSocket->GetRemoteAddr());
		m_pSocket = pSocket;
		return true;
	}

	virtual void OnDisconnect()
	{
		Log(LOG_INFO, "ECHO1: %s Disconnected\n", m_pSocket->GetRemoteAddr());
	}

	virtual void OnRelease()
	{
		m_pSocket = NULL;
	}

	virtual void OnSendBufferEmpty() {}

	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength)
	{
		m_pSocket->Send(pBuffer, nLength);
		return nLength;
	}

private:
	ITcpSocket* m_pSocket;
};


class Client2 : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client2(); }

	virtual void OnFinalDestruct()
	{
		delete this;
	}

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode)
	{
		Log(LOG_INFO, "ECHO2: Connected from %s\n", pSocket->GetRemoteAddr());
		m_pSocket = pSocket;
		return true;
	}

	virtual void OnDisconnect()
	{
		Log(LOG_INFO, "ECHO2: %s Disconnected\n", m_pSocket->GetRemoteAddr());
	}

	virtual void OnRelease()
	{
		m_pSocket = NULL;
	}

	virtual void OnSendBufferEmpty() {}

	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength)
	{
		for(unsigned int i=0; i<nLength; i++)
		{
			if(pBuffer[i] >= 'a' && pBuffer[i] <= 'z')
				pBuffer[i] = 'A' + (pBuffer[i] - 'a');
		}

		m_pSocket->Send(pBuffer, nLength);
		return nLength;
	}

private:
	ITcpSocket* m_pSocket;
};

class Module : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		pCore->RegisterClientListener(Client1::Creator, NULL, "echo1");
		pCore->RegisterClientListener(Client2::Creator, NULL, "echo2");
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(Module)
