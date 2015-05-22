#include "../samples.h"

#define JOBID_SELECT		1
#define JOBID_INSERT		2
#define JOBID_STOREDPROC	3

class Client;

struct JobSelect
{
	Client* pClient;
	INT32 sn;
	int numSelected;
};

struct JobInsert
{
	Client* pClient;
	INT64 uniqueId;
	INT16 power;
	float price;
	SqlDateTimeT creTime;
	WCHAR name[1];
	bool success;
};

struct JobSP
{
	Client* pClient;
	INT32 sn;
	INT16 power;
	float price;
	int result;
};

class DbThread : public IJobThreadListener
{
	virtual bool OnCreate();
	virtual void OnDestroy();
	virtual void OnDispatch(int nJobID, int nParam, void* pParam);
};


class Client : public BaseClientListener
{
public:
	Client();
	~Client();

	static ITcpClientListener* Creator(const void*) { return new Client(); }
	virtual void OnFinalDestruct() { delete this; }

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual void OnRelease();
	virtual void OnSendBufferEmpty() {}
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);

	void OnDbDone(int nJobId, void* pJob);

private:
	void HandlePacketString(char* pszMsg);
	ITcpSocket* m_pSocket;

	CriticalSection m_cs;
};

extern IServerCore* g_pCore;