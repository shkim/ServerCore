#include "jobthr.h"

bool JobProcessor::OnCreate()
{
	Log(LOG_INFO, "Job Processor created.\n");
	return true;
}

void JobProcessor::OnDestroy()
{
	Log(LOG_INFO, "Job Processor destroyed.\n");
}

void Capitalize(Client* pClient, int id, char* str)
{
	Sleep(1000); // faked long processing time

	char* p = str;
	while(*p != 0)
	{
		if(*p >= 'a' && *p <= 'z')
			*p = 'A' + (*p - 'a');
		p++;
	}

	char buff[512];
	StringCchPrintfA(buff, 512, "Capitalized %d: %s\r\n", id, str);
	pClient->m_pSocket->Send(buff, (int) strlen(buff));
}

void JobProcessor::OnDispatch(int nJobID, int nParam, void* pParam)
{
	switch(nJobID)
	{
	case JOBID_CAPITALIZE:
		SC_ASSERT(nParam == sizeof(Job1));
		{
			Job1* pJob = (Job1*) pParam;
			Capitalize(pJob->pClient, pJob->id, pJob->str);
			free(pJob);
		}
		break;

	default:
		Log(LOG_ERROR, "Invalid JobID: %d\n", nJobID);
	}
}
