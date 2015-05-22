#include "echo2.h"

class Echo2ServerModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		pCore->RegisterClientListener(Client::Creator);
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(Echo2ServerModule)

//////////////////////////////////////////////////////////////////////////////

bool Client::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	m_pSocket = pSocket;
	return true;
}

void Client::OnDisconnect()
{
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
	UINT cbUsed = 0;

_nextPacket:
	for(UINT i=0; i<nLength; i++)
	{
		if(pBuffer[i] == '\n' || pBuffer[i] == '\r')
		{
			if(i > 0)
			{
				pBuffer[i] = 0;
				HandlePacketString(pBuffer);
			}

			i++;
			cbUsed += i;
			pBuffer = &pBuffer[i];
			nLength -= i;

			goto _nextPacket;
		}
	}

	return cbUsed;
}

void Client::HandlePacketString(const char* pszMsg)
{
	char buffer[1024];

	Log(LOG_INFO, "Client: %s\n", pszMsg);

	StringCchPrintfA(buffer, 1024, "%s\r\n", pszMsg);
	m_pSocket->Send(buffer, (int) strlen(buffer));
}
