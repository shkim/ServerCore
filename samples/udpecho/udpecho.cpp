#include "../samples.h"

class UdpListener : public IUdpSocketListener
{
public:
	virtual void OnSocketCreated(IUdpSocket* pSocket, int nErrorCode)
	{
		if(pSocket == NULL)
		{
			// error
			return;
		}

		m_pSocket = pSocket;
	}

	virtual void OnReceive(const SockAddrT* pFrom, char* pBuffer, unsigned int nLength)
	{
		unsigned int nIpAddr;
		unsigned short nPort;
		m_pSocket->TranslateAddr(pFrom, &nIpAddr, &nPort);

		pBuffer[nLength] = 0;
		Log(LOG_INFO, "UdpEcho: %s (%d bytes from %d.%d.%d.%d:%d)\n",
			pBuffer, nLength,
			nIpAddr >> 24, (nIpAddr >> 16) & 0xFF,
			(nIpAddr >> 8) & 0xFF, nIpAddr & 0xFF, nPort);

		m_pSocket->Send(pFrom, pBuffer, nLength);
	}

	IUdpSocket* m_pSocket;
};

UdpListener g_udpLsnr;

class Module : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		pCore->RegisterUdpListener(&g_udpLsnr);
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(Module)
