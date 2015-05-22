#include "jobthr.h"

/*
	ServerCore Sample, Job (Worker) Thread

	Client: General telnet client (uses TCP port 23)
	Behavior:
		When Client sends a line to Server,
		Server queues a job to the job thread 
		and replies a notification message immediately.
		After the job is processed, job thread sends it to the client.
*/

IJobQueue* g_jobq;
JobProcessor s_jobproc;

bool Client::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	m_pSocket = pSocket;
	m_nJobSerial = 0;
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

void Client::HandlePacketString(const char* pszMsg)
{
	if(strlen(pszMsg) > 256)
	{
		// too long string
		m_pSocket->Kick();
		return;
	}

	Job1* pJob = (Job1*) malloc(sizeof(Job1));

	Log(LOG_INFO, "Client: %s\n", pszMsg);
	pJob->pClient = this;
	pJob->id = ++m_nJobSerial;
	StringCchCopyNA(pJob->str, 260, pszMsg, 256);
	g_jobq->Post(this, JOBID_CAPITALIZE, sizeof(Job1), pJob);

	char buff[256];
	StringCchPrintfA(buff, 256, "Job Queued: %d (%s)\r\n", m_nJobSerial, pszMsg);
	m_pSocket->Send(buff, (int) strlen(buff));
}

//////////////////////////////////////////////////////////////////////////////

class SampleModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		g_jobq = pCore->CreateJobThread(&s_jobproc);
		pCore->RegisterClientListener(Client::Creator);
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(SampleModule)
