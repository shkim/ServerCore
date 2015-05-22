#include "../samples.h"

#define JOBID_CAPITALIZE		1

class Client : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client(); }
	virtual void OnFinalDestruct() { delete this; }

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual void OnRelease();
	virtual void OnSendBufferEmpty() {}
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);

	ITcpSocket* m_pSocket;

private:
	void HandlePacketString(const char* pszMsg);
	int m_nJobSerial;
};

struct Job1
{
	Client* pClient;
	int id;
	char str[260];
};

class JobProcessor : public IJobThreadListener
{
	virtual bool OnCreate();
	virtual void OnDestroy();
	virtual void OnDispatch(int nJobID, int nParam, void* pParam);
};
