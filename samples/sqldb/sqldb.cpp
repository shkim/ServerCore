#include "sqldb.h"

/*
	ServerCore Sample - SQL Database

	Client: General telnet client (uses TCP port 23)
	Behavior:
		Database access is usually done in another (usually dedicated) thread.
		So this sample is similar with JobThread sample.
		Client can send one of db commands. ("select", "insert", "sp")
*/

IServerCore* g_pCore;
IJobQueue* g_dbq;
DbThread s_dbthr;

Client::Client()
{
	m_cs.Create();
}

Client::~Client()
{
	m_cs.Destroy();
}

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

void Client::HandlePacketString(char* pszMsg)
{
	Log(LOG_INFO, "Client: '%s'\n", pszMsg);

	char* params[8], *tt;
	char* cmd = strtok_s(pszMsg, " ", &tt);
	int numParams = 0;
	do
	{
		params[numParams] = strtok_s(NULL, " ", &tt);
		if (params[numParams] == NULL)
			break;
	} while (++numParams < 8);

	if (!strcmp(cmd, "select"))
	{
		JobSelect* pJob = (JobSelect*) malloc(sizeof(JobSelect));
		pJob->pClient = this;
		pJob->sn = (numParams > 0) ? atoi(params[0]) : 1;

		Log(LOG_DEBUG, "Select (where=%d) queued\n", pJob->sn);
		g_dbq->Post(this, JOBID_SELECT, 1, pJob);
	}
	else if (!strcmp(cmd, "insert"))
	{
		JobInsert* pJob = (JobInsert*) malloc(512);
		pJob->pClient = this;
		pJob->uniqueId = (numParams > 0) ? atoi(params[0]) : rand();
		pJob->power = rand();
		pJob->price = 0.1f * rand();
		pJob->creTime.year = 2015;
		pJob->creTime.month = 11;
		pJob->creTime.day = 30;
		pJob->creTime.hour = 13;
		pJob->creTime.minute = 59;
		pJob->creTime.second = 11;
		StringCchPrintfW(pJob->name, 256, L"String R%u", rand());
		g_dbq->Post(this, JOBID_INSERT, 2, pJob);
	}
	else if (!strcmp(cmd, "sp"))
	{
		JobSP* pJob = (JobSP*) malloc(sizeof(JobSP));
		pJob->pClient = this;
		pJob->sn = (numParams > 0) ? atoi(params[0]) : 1;
		pJob->power = rand();
		pJob->price = rand() * 0.5f;
		g_dbq->Post(this, JOBID_STOREDPROC, 3, pJob);
	}
	else if (!strcmp(cmd, "shutdown"))
	{
		g_pCore->Shutdown();
	}
	else if (!strcmp(cmd, "quit"))
	{
		m_pSocket->Kick();
	}
	else
	{
		char buffer[512];
		StringCchCopyA(buffer, 256, "Unknown command.\r\n");
		m_pSocket->Send(buffer, (int) strlen(buffer));
	}
}

void Client::OnDbDone(int nJobId, void* pJob)
{
	// If this is logically thread safe, lock is needless.
	m_cs.Lock();
	{
		// any logic processing
	}
	m_cs.Unlock();

	char buffer[128];
	switch (nJobId)
	{
	case JOBID_SELECT:
		StringCchPrintfA(buffer, 128, "ID %d: %d rows selected.\r\n", ((JobSelect*)pJob)->sn, ((JobSelect*)pJob)->numSelected);
		break;

	case JOBID_INSERT:
		StringCchPrintfA(buffer, 128, "Insert: %s\r\n", ((JobInsert*)pJob)->success ? "succeeded":"failed");
		break;

	case JOBID_STOREDPROC:
		StringCchPrintfA(buffer, 128, "SPP(%d) result=%d\r\n", ((JobSP*)pJob)->sn, ((JobSP*)pJob)->result);
		break;
	default:
		StringCchPrintfA(buffer, 128, "Invalid JobID=%d\r\n", nJobId);
	}

	// socket Send() is thread safe
	m_pSocket->Send(buffer, (int) strlen(buffer));
}

//////////////////////////////////////////////////////////////////////////////

class Module : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		g_dbq = pCore->CreateJobThread(&s_dbthr);
		pCore->RegisterClientListener(Client::Creator);
		g_pCore = pCore;
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(Module)
