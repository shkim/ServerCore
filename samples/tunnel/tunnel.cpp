#include "../samples.h"

IServerCore* g_pCore;

class TunnelClient;

#define LOW_SEND_BUFFER_LEN		2048
#define APP_BUFFER_LEN			8192

inline bool IsLowSendBuffer(ITcpSocket* pSocket)
{
	return (pSocket->GetPendingBytesToSend() < LOW_SEND_BUFFER_LEN);		
}

class AppBuffer
{
public:
	char m_buffer[APP_BUFFER_LEN];
	int m_used;

	void Reset()
	{
		m_used = 0;
	}

	int Receive(char* pBuffer, int nLength)
	{
		int cbCanRecv = APP_BUFFER_LEN - m_used;
		if (cbCanRecv > 0)
		{
			int cbRecv = (nLength < cbCanRecv) ? nLength : cbCanRecv;
			memcpy(&m_buffer[m_used], pBuffer, cbRecv);
			m_used += cbRecv;
			return cbRecv;
		}

		return 0;
	}

	inline void Send(ITcpSocket* pSocket)
	{
		if (m_used > 0)
		{
			pSocket->Send(m_buffer, m_used);
			m_used = 0;
		}
	}
};

class TargetConn : public ITcpSocketListener
{
public:
	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);
	virtual void OnSendBufferEmpty();


	bool Send(char* pBuffer, int len)
	{
		if (m_pSocket->GetPendingBytesToSend() < LOW_SEND_BUFFER_LEN)
		{
			m_pSocket->Send(pBuffer, len);
			return true;
		}

		return false;
	}

	ITcpSocket* m_pSocket;
	TunnelClient* m_pOwner;
	AppBuffer m_buffer;
};

class TunnelClient : public BaseClientListener
{
public:
	ITcpSocket* m_pSocket;
	TargetConn m_targetConn;
	AppBuffer m_buffer;
	const char* m_pszTargetServer;

	static ITcpClientListener* Creator(const void* param)
	{
		return new TunnelClient((const char*)param);
	}

	virtual void OnFinalDestruct()
	{
		delete this;
	}

	TunnelClient(const char* pszTargetServer)
	{
		m_pszTargetServer = pszTargetServer;
		m_targetConn.m_pOwner = this;
		m_targetConn.m_pSocket = NULL;
		m_pSocket = NULL;
	}

	virtual void OnRelease()
	{
		m_pSocket = NULL;
	}

	virtual void OnDisconnect()
	{
		if(m_targetConn.m_pSocket != NULL)
		{
			m_targetConn.m_pSocket->Kick();
		}
	}

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode)
	{
		char buff[256];
		StringCchCopyA(buff, 256, m_pszTargetServer);
		char* pColon = strchr(buff, ':');
		SC_ASSERT(pColon != NULL);
		*pColon = 0;
		int portnum = atoi(&pColon[1]);

		if(!g_pCore->Connect(&m_targetConn, buff, portnum))
		{
			Log(LOG_ERROR, "pCore->Connect(%s:%d) failed\n", buff, portnum);
			return false;
		}

		m_buffer.Reset();

		pSocket->SetCalledOnSendBufferEmpty(true);
		m_pSocket = pSocket;

		return true;
	}

	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);
	virtual void OnSendBufferEmpty();

	void OnTargetConnect()
	{
		Log(LOG_INFO|LOG_DATE, "Connected to %s (pending send: %d bytes)\n", m_pszTargetServer, m_buffer.m_used);
		m_buffer.Send(m_targetConn.m_pSocket);
	}

	void OnTargetDisconnect()
	{
		if (m_pSocket != NULL)
		{
			Log(LOG_INFO|LOG_DATE, "Target(%s) connection closed.\n", m_pszTargetServer);
			m_pSocket->Kick();
		}
	}
};

bool TargetConn::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	if (pSocket == NULL)
	{
		// failed to connect to target
		m_pOwner->OnTargetDisconnect();
		return false;
	}

	pSocket->SetCalledOnSendBufferEmpty(true);
	m_pSocket = pSocket;

	m_pOwner->OnTargetConnect();
	m_pOwner->Ref_Retain();

	return true;
}

void TargetConn::OnDisconnect()
{
	m_pSocket = NULL;
	m_pOwner->OnTargetDisconnect();
	m_pOwner->Ref_Release();
}

unsigned int TunnelClient::OnReceive(char* pBuffer, unsigned int nLength)
{
	Log(LOG_VERBOSE, "TunnelClient::OnReceive(%d)\n", nLength);

	if (m_targetConn.m_pSocket == NULL || !IsLowSendBuffer(m_targetConn.m_pSocket))
	{
		return m_buffer.Receive(pBuffer, nLength);
	}
	else
	{
		m_buffer.Send(m_targetConn.m_pSocket);
		m_targetConn.m_pSocket->Send(pBuffer, nLength);
		return nLength;
	}
}

unsigned int TargetConn::OnReceive(char* pBuffer, unsigned int nLength)
{
	if (m_pOwner->m_pSocket == NULL)
	{
		Log(LOG_ERROR, "Owner's socket is NULL. (maybe disconnected)\n");
		m_pSocket->Kick();
		return 0;
	}

	Log(LOG_VERBOSE, "TargetConn::OnReceive(%d)\n", nLength);

	if (IsLowSendBuffer(m_pOwner->m_pSocket))
	{
		m_buffer.Send(m_pOwner->m_pSocket);
		m_pOwner->m_pSocket->Send(pBuffer, nLength);
		return nLength;
	}
	else
	{
		return m_buffer.Receive(pBuffer, nLength);
	}
}

void TargetConn::OnSendBufferEmpty()
{
	Log(LOG_VERBOSE, "TargetConn::OnSendBufferEmpty\n");

	if (m_pOwner->m_pSocket != NULL)
	{
		m_pOwner->m_buffer.Send(m_pSocket);
		m_pOwner->m_pSocket->RequestReceiveBuffer();
	}
}

void TunnelClient::OnSendBufferEmpty()
{
	Log(LOG_VERBOSE, "TunnelClient::OnSendBufferEmpty\n");

	if (m_targetConn.m_pSocket != NULL)
	{
		m_targetConn.m_buffer.Send(m_pSocket);
		m_targetConn.m_pSocket->RequestReceiveBuffer();
	}
}

//////////////////////////////////////////////////////////////////////////

class TheModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		g_pCore = pCore;

		IConfigSection* pConfig = pCore->GetConfigSection("ServerCore");
		if (pConfig == NULL)
		{
			Log(LOG_FATAL, "[ServerCore] section not found.\n");
			return false;
		}

		const char* servers = pConfig->GetString("servers");
		if (servers == NULL)
		{
			Log(LOG_FATAL, "No 'Servers' directive.\n");
			return false;
		}

		char buff[256];
		char *tok, *tt;

		StringCchCopyA(buff, 256, servers);
		tok = strtok_s(buff, ", ", &tt);
		while(tok)
		{
			pConfig = pCore->GetConfigSection(tok);
			if (pConfig == NULL)
			{
				Log(LOG_FATAL, "Tunnel definition [%s] not found.\n", tok);
				return false;
			}

			const char* target = pConfig->GetString("DestinationHost");
			if (target == NULL)
			{
				Log(LOG_FATAL, "No 'TargetServer' directive in [%s]\n", tok);
				return false;
			}

			if (strchr(target, ':') == NULL)
			{
				Log(LOG_FATAL, "Invalid TargetServer value '%s' in [%s]\n", target, tok);
				return false;
			}

			pCore->RegisterClientListener(TunnelClient::Creator, target, tok);

			tok = strtok_s(NULL, ", ", &tt);
		}

		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(TheModule)
