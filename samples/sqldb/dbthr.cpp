#include "sqldb.h"

// see README.txt for sql table initialization

static ISqlStmt* s_psSelect;
static ISqlStmt* s_psInsert;
static ISqlStmt* s_psStoredProc;

static int s_serialNum;
static __int64 s_uniqueId;
static int s_power;
static int s_result;
static float s_price;
static SqlDateTimeT s_cretime;
static WCHAR s_name[120];

bool DbThread::OnCreate()
{
	ISqlConn* pConn = g_pCore->GetSqlConnection();
	
	pConn->CreateStatement("select1", &s_psSelect, true);
	s_psSelect->BindParam("sn", &s_serialNum);
	s_psSelect->BindColumn("uniqueid", &s_uniqueId);
	s_psSelect->BindColumn("power", &s_power);
	s_psSelect->BindColumn("price", &s_price);
	s_psSelect->BindColumn("cretime", &s_cretime);
	s_psSelect->BindColumn("name", s_name, 120);

	pConn->CreateStatement("insert1", &s_psInsert, false);
	s_psInsert->BindParam("uniqueid", &s_uniqueId);
	s_psInsert->BindParam("name", s_name, 120);		
	s_psInsert->BindParam("price", &s_price);
	s_psInsert->BindParam("power", &s_power);
	s_psInsert->BindParam("cretime", &s_cretime);

	pConn->CreateStatement("sp1", &s_psStoredProc, true);
	s_psStoredProc->BindParam("sn", &s_serialNum);
	s_psStoredProc->BindParam("power", &s_power);
	s_psStoredProc->BindParam("price", &s_price);
	s_psStoredProc->BindColumn("result", &s_result);

	return true;
}

void DbThread::OnDestroy()
{
}

void DJ_Select(JobSelect* pJob)
{
	s_serialNum = pJob->sn;
	pJob->numSelected = 0;
	if(s_psSelect->Execute())
	{
		while(s_psSelect->Fetch())
		{
			Log(LOG_INFO, "Select uid=%lld, power=%d, price=%f, name=%S\n", s_uniqueId, s_power, s_price, s_name);
			pJob->numSelected++;
		}

		s_psSelect->Close();
	}

	pJob->pClient->OnDbDone(JOBID_SELECT, pJob);
}

void DJ_Insert(JobInsert* pJob)
{
	s_uniqueId = pJob->uniqueId;
	s_price = pJob->price;
	s_power = pJob->power;
	s_cretime = pJob->creTime;
	StringCchCopyW(s_name, 256, pJob->name);

	if (pJob->success = s_psInsert->Execute())
	{
		s_psInsert->Close();
	}

	pJob->pClient->OnDbDone(JOBID_INSERT, pJob);
}

void DJ_StoreProc(JobSP* pJob)
{
	s_serialNum = pJob->sn;
	s_power = pJob->power;
	s_price = pJob->price;
	if(s_psStoredProc->Execute())
	{
		if(s_psStoredProc->Fetch())
		{
			Log(LOG_INFO, "StoredProc fetched, result=%d\n", s_result);
			pJob->result = s_result;
		}
		else
		{
			Log(LOG_ERROR, "StoredProc fetch failed\n");
		}

		s_psStoredProc->Close();
	}

	pJob->pClient->OnDbDone(JOBID_STOREDPROC, pJob);
}

void DbThread::OnDispatch(int nJobID, int nParam, void* pParam)
{
	switch(nJobID)
	{
	case JOBID_SELECT:
		DJ_Select((JobSelect*)pParam);
		break;

	case JOBID_INSERT:
		DJ_Insert((JobInsert*)pParam);
		break;

	case JOBID_STOREDPROC:
		DJ_StoreProc((JobSP*)pParam);
		break;

	default:
		Log(LOG_FATAL, "Invalid JobID: %d\n", nJobID);
	}

	free(pParam);
}
