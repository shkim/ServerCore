#include "stdafx.h"
#include "scimpl.h"

// FIXME
#define ScPoolAlloc		malloc
#define ScPoolFree		free

SC_NAMESPACE_BEGIN

extern IConfigSection* g_pCoreConfigSection;
static SQLLEN s_tempLenOrInd;

/*
class FakeConn : public ISqlConn
{
	virtual bool Commit() { return false; }
	virtual void Rollback() {}
	virtual bool CreateStatement(const char* pszQueryName, ISqlStmt** ppOut) { return false; }
	virtual bool ExecuteSql(const char* pszNonQuerySQL) { return false; }
};

static FakeConn s_fakeConn;
*/

#ifdef SC_DISABLE_ODBC

bool ServerCore::OpenDatabase()
{
	if(NULL == g_pCoreConfigSection->GetString("Databases"))
	{
		// database not specified in config
		return true;
	}

	Log(LOG_SYSTEM, "ODBC support is disabled in this ServerCore build.\n");
	return false;
}

void ServerCore::CloseDatabase()
{
}

void ServerCore::CloseDbStatements()
{
}

void ServerCore::OnInvalidDbApiCall()
{
}

ISqlConn* ServerCore::GetSqlConnection(const char* pszConnName)
{
	return NULL;//&s_fakeConn;
}

#else

#define BINDCOL_UNKNOWN		0
#define BINDCOL_NAMED		1
#define BINDCOL_INDEXED		2

class FakeStatement : public ISqlStmt
{
public:
	virtual void Destroy() {}
	virtual bool Execute(int*) { return false; }
	virtual bool Fetch() { return false; }
	virtual void Close() {}

	virtual int GetColumnCount() { return 0; }
	virtual ISqlConn* GetSqlConnection() { return NULL; }
	virtual bool NextResult() { return false; }

	virtual void BindParam(const char *name, short* param) {}
	virtual void BindParam(const char *name, int* param) {}
	virtual void BindParam(const char *name, __int64* param) {}
	virtual void BindParam(const char *name, float* param) {}
	virtual void BindParam(const char *name, char* param, int len) {}
	virtual void BindParam(const char *name, WCHAR* param, int len) {}
	virtual void BindParam(const char *name, SqlDateT* param) {}
	virtual void BindParam(const char *name, SqlTimeT* param) {}
	virtual void BindParam(const char *name, SqlDateTimeT* param) {}


	virtual void BindColumn(const char *name, short *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, int *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, __int64 *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, float *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, char* outbuf, int buflen, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, WCHAR* outbuf, int buflen, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, SqlDateT* outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, SqlTimeT* outbuf, int* pLenOrInd) {}
	virtual void BindColumn(const char *name, SqlDateTimeT* outbuf, int* pLenOrInd) {}

	virtual void BindColumn(int nColumnIndex, short *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, int *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, __int64 *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, float *outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, char* outbuf, int buflen, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, WCHAR* outbuf, int buflen, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, SqlDateT* outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, SqlTimeT* outbuf, int* pLenOrInd) {}
	virtual void BindColumn(int nColumnIndex, SqlDateTimeT* outbuf, int* pLenOrInd) {}
};

static FakeStatement s_fakeStmt;
static SQLHENV s_hEnv = NULL;

inline bool IsSqlSuccess(SQLRETURN ret)
{
	return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);		
}

bool IsSqlSuccessV(SQLRETURN ret, SQLHANDLE hStmt)
{
	if (ret == SQL_SUCCESS)
		return true;

	if (ret == SQL_SUCCESS_WITH_INFO)
	{
		SQLSMALLINT MsgLen, i = 1;
		SQLRETURN ret;
		SQLCHAR SqlState[7], Msg[SQL_MAX_MESSAGE_LENGTH];
		SQLINTEGER NativeError;

		while((ret = SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, i, SqlState, 
			&NativeError, Msg, sizeof(Msg), &MsgLen)) != SQL_NO_DATA)
		{
			Log(LOG_VERBOSE, "%s %s (%d)\n", SqlState, Msg, NativeError);
			i++;
		}

		return true;
	}

	return false;
}

PreparedStatement::PreparedStatement(DbConn* pConn, const char* pszOrigQuery, bool bIsSelectQuery)
{
	m_hStmt = NULL;
	m_pConn = pConn;

	//m_bCloseCursorOnEnd = false;
	m_bIsSelectQuery = bIsSelectQuery;
	m_bBindParamDone = false;
	m_bBindColumnDone = false;
	m_bParamArrayAdjusted = false;
	m_nColumnBindType = BINDCOL_UNKNOWN;

	m_pszOriginalQuery = pszOrigQuery;
	m_nParamCount = 0;

	// estimate parameter count
	int nSharpCount = 0;
	const char* p = pszOrigQuery;
	for(; *p != 0; p++)
	{
		if(*p == '#')
			nSharpCount++;
	}

	if(nSharpCount > 0)
	{
		size_t nOrigLen = p - pszOrigQuery +1;
		size_t cbReq = nOrigLen + (nSharpCount/2) 
			* (sizeof(ParamUserArg) + sizeof(ParamUserArg*)) + 8;
		m_pszEdittedSQL = (char*) ScPoolAlloc((int)cbReq);
		memcpy(m_pszEdittedSQL, pszOrigQuery, nOrigLen);
		
		p = &m_pszEdittedSQL[nOrigLen];
		m_aParamUserArgs = (ParamUserArg*) ((((size_t)p) + 3) & ~3);
		m_nParamCount = 0;
	}
	else
	{
		m_pszEdittedSQL = const_cast<char*>(m_pszOriginalQuery);
	}
}

PreparedStatement::~PreparedStatement()
{
	SC_ASSERT(m_hStmt == NULL);

	if(m_nParamCount > 0)
	{
		SC_ASSERT(m_pszEdittedSQL != NULL);
		ScPoolFree(m_pszEdittedSQL);
	}
}

void PreparedStatement::FreeHandle()
{
	if(m_hStmt != NULL)
	{
		SQLFreeStmt(m_hStmt, SQL_CLOSE);
		SQLFreeHandle(SQL_HANDLE_STMT, m_hStmt);
		m_hStmt = NULL;
	}
}

void PreparedStatement::Destroy()
{
	FreeHandle();
	m_pConn->ErasePreparedStatement(this);
}

bool PreparedStatement::Restore()
{
	m_bBindParamDone = false;
	m_bBindColumnDone = false;

	std::vector<ColumnUserArg>::iterator itr = m_vecNamedColumns.begin();
	for(; itr != m_vecNamedColumns.end(); ++itr)
	{
		ColumnUserArg& col = *itr;
		col.bColumnFound = false;
	}

	if(BindNow())
		return true;

	Log(LOG_FATAL, "SQL statement restore fail: %s\n", m_pszEdittedSQL);
	return false;
}

//////////////////////////////////////////////////////////////////////////////

void PreparedStatement::BindParam(const char *name, short* param)
{
	CollectParamUserArg(name, SQL_SMALLINT, SQL_C_SHORT, param, sizeof(short));
}

void PreparedStatement::BindParam(const char *name, int* param)
{
	CollectParamUserArg(name, SQL_INTEGER, SQL_C_LONG, param, sizeof(long));
}

void PreparedStatement::BindParam(const char *name, __int64* param)
{
	CollectParamUserArg(name, SQL_BIGINT, SQL_C_SBIGINT, param, sizeof(__int64));
}

void PreparedStatement::BindParam(const char *name, float* param)
{
	CollectParamUserArg(name, SQL_FLOAT, SQL_C_FLOAT, param, sizeof(float));
}

void PreparedStatement::BindParam(const char *name, char* param, int len)
{
	//CollectParamUserArg(name, SQL_VARCHAR, SQL_C_CHAR, param, len);
	CollectParamUserArg(name, SQL_CHAR, SQL_C_CHAR, param, len);
}

void PreparedStatement::BindParam(const char *name, WCHAR* param, int len)
{
	//CollectParamUserArg(name, SQL_WVARCHAR, SQL_C_WCHAR, param, len * sizeof(WCHAR));
	CollectParamUserArg(name, SQL_WCHAR, SQL_C_WCHAR, param, len * sizeof(WCHAR));
}

void PreparedStatement::BindParam(const char *name, IN SqlDateT* param)
{
	CollectParamUserArg(name, SQL_TYPE_DATE, SQL_C_TYPE_DATE, param, sizeof(SqlDateT));
}

void PreparedStatement::BindParam(const char *name, IN SqlTimeT* param)
{
	CollectParamUserArg(name, SQL_TYPE_TIME, SQL_C_TYPE_TIME, param, sizeof(SqlTimeT));
}

void PreparedStatement::BindParam(const char *name, IN SqlDateTimeT* param)
{
	CollectParamUserArg(name, SQL_TYPE_TIMESTAMP, SQL_C_TYPE_TIMESTAMP, param, sizeof(SqlDateTimeT));
}

void PreparedStatement::CollectParamUserArg(const char *name, int sqltype, int ctype, void* pBuffer, int nBufferLen)
{
	SC_ASSERT(m_hStmt == NULL && m_bBindParamDone == false);// && m_bErrorOccur == false);
	char marker[128];

	if(name == NULL || name[0] == 0)
	{
		Log(LOG_FATAL, "BindParam failed: null name\n");
		g_core.OnInvalidDbApiCall();
		return;
	}

	StringCchPrintfA(marker, 128, "#%s#", name);
	char* pos = strstr(m_pszEdittedSQL, marker);

	if(pos == NULL)
	{
		Log(LOG_FATAL, "[%s] Couldn't find the parameter marker %s in %s\n", m_pConn->m_szName, marker, m_pszOriginalQuery);
		g_core.OnInvalidDbApiCall();
		return;
	}

	ParamUserArg* pSource = &m_aParamUserArgs[m_nParamCount];
	m_nParamCount++;

	pSource->pos = pos;
	pSource->marker_len = (int) strlen(marker);	
	memset(pos, ' ', pSource->marker_len);
	pos[0] = '?';

	pSource->param_sqltype = sqltype;
	pSource->param_ctype = ctype;
	pSource->param_length = (short) nBufferLen;
	pSource->param_ptr = pBuffer;
}

bool PreparedStatement::BindNow()
{
	SQLRETURN ret;
	int i;

	SC_ASSERT(m_bBindParamDone == false);

	if(m_bIsSelectQuery == false && m_nColumnBindType != BINDCOL_UNKNOWN)
	{
		Log(LOG_ERROR, "NonSelect query called BindColumn: %s\n", m_pszOriginalQuery);
		return false;
	}

	if(m_nParamCount > 0 && m_bParamArrayAdjusted == false)
	{
		ParamUserArg** aSorted = (ParamUserArg**) &m_aParamUserArgs[m_nParamCount];

		aSorted[0] = &m_aParamUserArgs[0];
		if(m_nParamCount > 1)
		{
			for(i=1; i<m_nParamCount; i++)
			{
				aSorted[i] = &m_aParamUserArgs[i];
			}

			// simply do bubble sort
			for(int lim=m_nParamCount; lim > 1; lim--)
			{
				for(i=1; i<lim; i++)
				{
					if(aSorted[i-1]->pos > aSorted[i]->pos)
					{
						ParamUserArg* tmp = aSorted[i-1];
						aSorted[i-1] = aSorted[i];
						aSorted[i] = tmp;
					}
				}
			}
		}

		int shifted = 0;
		char* endpos = (char*) m_aParamUserArgs -1;
		for(i=0; i<m_nParamCount; i++)
		{
			char* p = aSorted[i]->pos;
			size_t movelen = endpos - p;
			p -= shifted;
			memmove(&p[1], &p[aSorted[i]->marker_len], movelen);
			shifted += aSorted[i]->marker_len -1;
		}

		int cbReq = (int) strlen(m_pszEdittedSQL);
		i = 1 + cbReq;
		cbReq += m_nParamCount * sizeof(ParamBindInfo) + 32;
		char* pReAlloc = (char*)ScPoolAlloc(cbReq);
		
		memcpy(pReAlloc, m_pszEdittedSQL, i);
		endpos = pReAlloc + i;
		m_aParamBindInfo = (ParamBindInfo*) ((((size_t)endpos) + 3) & ~3);
		for(i=0; i<m_nParamCount; i++)
		{
			m_aParamBindInfo[i].ptr = aSorted[i]->param_ptr;
			m_aParamBindInfo[i].bufflen = aSorted[i]->param_length;
			m_aParamBindInfo[i].ctype = aSorted[i]->param_ctype;
			m_aParamBindInfo[i].sqltype = aSorted[i]->param_sqltype;

			switch(aSorted[i]->param_sqltype)
			{
			case SQL_CHAR:
			case SQL_WCHAR:
			case SQL_VARCHAR:
			case SQL_WVARCHAR:
				m_aParamBindInfo[i].sqllen = SQL_NTS;
				break;
			default:
				m_aParamBindInfo[i].sqllen = m_aParamBindInfo[i].bufflen;
			}
		}

		ScPoolFree(m_pszEdittedSQL);
		m_pszEdittedSQL = pReAlloc;
		m_bParamArrayAdjusted = true;
	}

	SC_ASSERT(m_hStmt == NULL);// && m_bErrorOccur == false);

	ret = SQLAllocHandle(SQL_HANDLE_STMT, m_pConn->m_hDBC, &m_hStmt);
	if(!IsSqlSuccess(ret))
	{
		Log(LOG_FATAL, "ODBC: Statement handle allocation failed (%s)\n", m_pszEdittedSQL);
		g_core.OnInvalidDbApiCall();
		return false;
	}

	ret = SQLPrepare(m_hStmt, (SQLCHAR*) m_pszEdittedSQL, (SQLINTEGER) strlen(m_pszEdittedSQL));
	if(!IsSqlSuccess(ret))
	{
		PrintSqlError();
		Log(LOG_FATAL, "ODBC: SQLPrepare failed: %s (%s)\n", m_pszEdittedSQL, m_pszOriginalQuery);
		g_core.OnInvalidDbApiCall();
		return false;
	}

	for(i=0; i<m_nParamCount; i++)
	{
		ret = SQLBindParameter(m_hStmt, (SQLUSMALLINT) (i+1), SQL_PARAM_INPUT, 
			m_aParamBindInfo[i].ctype,
			m_aParamBindInfo[i].sqltype,
			m_aParamBindInfo[i].bufflen,
			0, m_aParamBindInfo[i].ptr,
			0, &m_aParamBindInfo[i].sqllen);

		if(IsSqlSuccess(ret) == false)
		{
			Log(LOG_FATAL, "ODBC: SQLBindParameter(index=%d) failed in '%s'\n", i, m_pszEdittedSQL);
			PrintSqlError();
			g_core.OnInvalidDbApiCall();
			return false;
		}
	}

	SQLSMALLINT nParams;
	ret = SQLNumParams(m_hStmt, &nParams);

	if(!IsSqlSuccess(ret))
	{
		PrintSqlError();
		g_core.OnInvalidDbApiCall();
		return false;
	}

	if(nParams != m_nParamCount)
	{
		Log(LOG_FATAL, "Parameter count not match: SQLNumParams(%d) != BindParam(%d)\n\tin SQL: %s\n", nParams, m_nParamCount, m_pszEdittedSQL);
		g_core.OnInvalidDbApiCall();
		return false;
	}

	if(m_nColumnBindType == BINDCOL_INDEXED)
	{
		SC_ASSERT(!m_vecNamedColumns.empty());
	}

	m_bBindParamDone = true;
	return true;
}

//////////////////////////////////////////////////////////////////////////////

void PreparedStatement::BindColumn(const char* name, short *outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_SHORT, outbuf, sizeof(short), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, int *outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_LONG, outbuf, sizeof(int), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, __int64 *outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_SBIGINT, outbuf, sizeof(__int64), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, float *outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_FLOAT, outbuf, sizeof(float), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, char* outbuf, int buflen, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_CHAR, outbuf, buflen, pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, WCHAR* outbuf, int buflen, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_WCHAR, outbuf, buflen, pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, SqlDateT* outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_TYPE_DATE, outbuf, sizeof(SqlDateT), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, SqlTimeT* outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_TYPE_TIME, outbuf, sizeof(SqlTimeT), pLenOrInd);
}

void PreparedStatement::BindColumn(const char* name, SqlDateTimeT* outbuf, int* pLenOrInd)
{
	CollectColumnUserArg(name, SQL_C_TYPE_TIMESTAMP, outbuf, sizeof(SqlDateTimeT), pLenOrInd);
}

void PreparedStatement::CollectColumnUserArg(const char* name, int ctype, void* pBuffer, int nBufferLen, int* pLenOrInd)
{
	if(m_nColumnBindType == BINDCOL_INDEXED)
	{
		Log(LOG_FATAL, "BindCol: Index and Name can't be used together.\n");
		g_core.OnInvalidDbApiCall();
		return;
	}

	std::vector<ColumnUserArg>::const_iterator itr = m_vecNamedColumns.begin();
	for(; itr != m_vecNamedColumns.end(); ++itr)
	{
		if(strcmp(itr->name, name) == 0)
		{
			Log(LOG_FATAL, "[%s] BindColumn(%s) duplicated.\n", m_pConn->m_szName, name);
			g_core.OnInvalidDbApiCall();
			return;
		}
	}

	ColumnUserArg col;
	col.name = name;
	col.ptr = pBuffer;
	col.pLenOrInd = pLenOrInd == NULL ? &s_tempLenOrInd : (SQLLEN*) pLenOrInd;
	col.ctype = ctype;
	col.length = nBufferLen;
	col.bColumnFound = false;
	m_vecNamedColumns.push_back(col);

	m_nColumnBindType = BINDCOL_NAMED;
}

//////////////////////////////////////////////////////////////////////////////

void PreparedStatement::BindColumn(int nColumnIndex, short *outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_SHORT, outbuf, sizeof(short), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, int *outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_LONG, outbuf, sizeof(int), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, __int64 *outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_SBIGINT, outbuf, sizeof(__int64), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, float *outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_FLOAT, outbuf, sizeof(float), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, char* outbuf, int buflen, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_CHAR, outbuf, buflen, pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, WCHAR* outbuf, int buflen, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_WCHAR, outbuf, buflen, pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, SqlDateT* outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_TYPE_DATE, outbuf, sizeof(SqlDateT), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, SqlTimeT* outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_TYPE_TIME, outbuf, sizeof(SqlTimeT), pLenOrInd);
}

void PreparedStatement::BindColumn(int nColumnIndex, SqlDateTimeT* outbuf, int* pLenOrInd)
{
	_BindColumn(nColumnIndex, SQL_C_TYPE_TIMESTAMP, outbuf, sizeof(SqlDateTimeT), pLenOrInd);
}

void PreparedStatement::_BindColumn(int nColumnIndex, int ctype, void* pBuffer, int nBufferLen, int* pLenOrInd)
{
	SQLRETURN ret;

	if(m_hStmt == NULL)
	{
		if(m_nColumnBindType == BINDCOL_NAMED)
		{
			Log(LOG_FATAL, "[%s] BindColumn: Index and Name can't be used together in %s\n", m_pConn->m_szName, m_pszOriginalQuery);
			g_core.OnInvalidDbApiCall();
			return;
		}

		// collect
		ColumnUserArg col;
		col.index = nColumnIndex;
		col.ptr = pBuffer;
		col.pLenOrInd = pLenOrInd == NULL ? &s_tempLenOrInd : (SQLLEN*) pLenOrInd;
		col.ctype = ctype;
		col.length = nBufferLen;
		col.bColumnFound = false;
		m_vecNamedColumns.push_back(col);

		m_nColumnBindType = BINDCOL_INDEXED;
	}
	else
	{
		// apply now
		ret = SQLBindCol(m_hStmt, (SQLUSMALLINT) nColumnIndex,
			(SQLSMALLINT) ctype, pBuffer, nBufferLen,
			pLenOrInd == NULL ? &s_tempLenOrInd : (SQLLEN*) pLenOrInd);

		if(!IsSqlSuccess(ret))
		{
			PrintSqlError();
//			m_bErrorOccur = true;
		}
	}
}

bool PreparedStatement::Fetch()
{
	SQLRETURN ret = SQLFetch(m_hStmt);

	if(ret == SQL_NO_DATA)
	{
		return false;
	}
	else if(IsSqlSuccessV(ret, m_hStmt))
	{
		return true;
	}
	else
	{
		Log(LOG_FATAL, "[%s] SQLFetch error %d: %s\n", m_pConn->m_szName, ret, m_pszOriginalQuery);
		PrintSqlError();
		return false;
	}
}

bool PreparedStatement::NextResult()
{
	SQLRETURN ret = SQLMoreResults(m_hStmt);
	
	if(ret == SQL_NO_DATA)
	{
		return false;
	}
	else if(IsSqlSuccessV(ret, m_hStmt))
	{
		return true;
	}
	else
	{
		Log(LOG_FATAL, "[%s] SQLMoreResults error %d: %s\n", m_pConn->m_szName, ret, m_pszOriginalQuery);
		PrintSqlError();
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////////

bool PreparedStatement::Execute(int* pAffectedRowCount)
{
	SQLRETURN ret;

	if(pAffectedRowCount != NULL)
	{
		*pAffectedRowCount = 0;
	}

	if(m_bBindParamDone == false)
	{
		if(!BindNow())
			return false;
	}

	if(m_pConn->m_nOdbcCallerSP > 0)
	{
		Log(LOG_ERROR, "WARNING: Previous SQL execution not ended:\n");
		for(int i=0; i<m_pConn->m_nOdbcCallerSP; i++)
		{
			Log(LOG_ERROR, "ODBC Call Stack [%d]: %s\n", i, 
				m_pConn->m_OdbcCallerStack[i].pStmt->m_pszOriginalQuery);
		}
		Log(LOG_ERROR, "ODBC Call Stack Now: %s\n", m_pszOriginalQuery);
	}

	if(m_pConn->m_bDbLinkFailure)
	{
		Log(LOG_ERROR, "DB-LINK ERROR! On execute %s\n", m_pszOriginalQuery);
		goto _restoreNow;
	}

	ret = SQLExecute(m_hStmt);

	if(false == IsSqlSuccess(ret))
	{
		PrintSqlError();

		if(m_pConn->m_bDbLinkFailure)
		{
_restoreNow:
			if(m_pConn->RestoreConnection())
			{
				// reconnect successful
				ret = SQLExecute(m_hStmt);
				if(IsSqlSuccessV(ret, m_hStmt))
					goto _execSuccess;
			}
		}

		Log(LOG_FATAL, "SQLExecute failed: %s\n", m_pszOriginalQuery);
		return false;
	}

_execSuccess:

	if(pAffectedRowCount != NULL)
	{
		if(m_bIsSelectQuery)
		{
			Log(LOG_WARN, "SQLRowCount is not available for SELECT query: %s", m_pszOriginalQuery);
		}
		else
		{
			SQLLEN rowCount;
			ret = SQLRowCount(m_hStmt, &rowCount);
			if(!IsSqlSuccessV(ret, m_hStmt))
				return false;
			
			*pAffectedRowCount = (int) rowCount;
		}
	}

	if(m_bBindColumnDone == false)
	{
		if(m_nColumnBindType == BINDCOL_INDEXED)
		{
			std::vector<ColumnUserArg>::iterator itr = m_vecNamedColumns.begin();
			for(; itr != m_vecNamedColumns.end(); ++itr)
			{
				ColumnUserArg& col = *itr;

				ret = SQLBindCol(m_hStmt, (SQLUSMALLINT) col.index,
					col.ctype, col.ptr, col.length, col.pLenOrInd);

				if(!IsSqlSuccess(ret))
				{
					PrintSqlError();
					return false;
				}
			}
		}
		else if(m_nColumnBindType == BINDCOL_NAMED)
		{
			char szMissingColumns[MAX_PATH];
			int nMissingLen;
			SQLCHAR szColumnName[128];
			SQLSMALLINT nColumnCount;
			SQLSMALLINT nColNameLen;

			// not used
			SQLSMALLINT nDataType;
			SQLULEN nColumnSize;
			SQLSMALLINT nDecimalDigits;
			SQLSMALLINT nNullable;			

			ret = SQLNumResultCols(m_hStmt, &nColumnCount);
			if(!IsSqlSuccess(ret))
			{
				Log(LOG_FATAL, "[%s] SQLNumResultCols failed(%d) in %s\n", m_pConn->m_szName, ret, m_pszEdittedSQL);
				PrintSqlError();
				return false;
			}

			nMissingLen = 0;
			for(int n=0; n<nColumnCount; n++)
			{
				SQLUSMALLINT nColumnIndex = n + 1;

				ret = SQLDescribeCol(m_hStmt, nColumnIndex, szColumnName, 128, &nColNameLen,
					&nDataType, &nColumnSize, &nDecimalDigits, &nNullable); 

				if(!IsSqlSuccess(ret))
				{
					Log(LOG_FATAL, "[%s] SQLDescribeCol(%d) error %d in %s\n",
						m_pConn->m_szName, nColumnIndex, ret, m_pszOriginalQuery);
					PrintSqlError();
					return false;
				}

				std::vector<ColumnUserArg>::iterator itr = m_vecNamedColumns.begin();
				for(; itr != m_vecNamedColumns.end(); ++itr)
				{
					ColumnUserArg& col = *itr;
					if(col.name[nColNameLen] == 0 && _stricmp(col.name, (const char*) szColumnName) == 0)
					{
						ret = SQLBindCol(m_hStmt, nColumnIndex,
							col.ctype, col.ptr, col.length, col.pLenOrInd);
						SC_ASSERT(IsSqlSuccessV(ret, m_hStmt));
						goto _nameFound;
					}
				}

				if(nMissingLen == 0)
				{
					StringCchCopy(szMissingColumns, MAX_PATH, (char*) szColumnName);
				}
				else
				{
					szMissingColumns[nMissingLen++] = ',';
					StringCchCopy(&szMissingColumns[nMissingLen], MAX_PATH - nMissingLen, (char*) szColumnName);
				}

				nMissingLen += nColNameLen;

_nameFound:
				;
			}

			if(nMissingLen > 0)
			{
				Log(LOG_WARN, "[%s] Column <%s> not bound in %s\n",
					m_pConn->m_szName, szMissingColumns, m_pszOriginalQuery);
			}
		}

		m_bBindColumnDone = true;
	}

	if(m_pConn->m_nOdbcCallerSP < MAX_ODBC_CALLER_STACK)
	{
		m_pConn->m_OdbcCallerStack[m_pConn->m_nOdbcCallerSP].nThreadID = ScGetThreadIndex();
		m_pConn->m_OdbcCallerStack[m_pConn->m_nOdbcCallerSP].pStmt = this;
		m_pConn->m_nOdbcCallerSP++;
	}

	return true;
}

void PreparedStatement::Close()
{
	if(m_pConn->m_nOdbcCallerSP <= 0)
	{
		Log(LOG_FATAL, "PrepStmt.Close: Not executed before: %s\n", m_pszOriginalQuery);
	}
	else
	{
		m_pConn->m_nOdbcCallerSP--;
		DWORD nThreadID = ScGetThreadIndex();
		if(m_pConn->m_OdbcCallerStack[m_pConn->m_nOdbcCallerSP].nThreadID != nThreadID)
		{
			Log(LOG_FATAL, "PrepStmt.Close: Close thread %d is diff from Execute %d, SQL=%s\n",
				nThreadID, m_pConn->m_OdbcCallerStack[m_pConn->m_nOdbcCallerSP].nThreadID, m_pszOriginalQuery);
		}
	}

	if(m_bIsSelectQuery)
	{
		SQLRETURN ret = SQLCloseCursor(m_hStmt);
		if(!IsSqlSuccess(ret))
		{
			Log(LOG_FATAL, "PrepStmt.Close: CloseCursor failed: %s (%d)\n", m_pszOriginalQuery, ret);
			PrintSqlError();
		}
	}
}

void PreparedStatement::PrintSqlError()
{
	SQLSMALLINT MsgLen, i = 1;
	SQLRETURN ret;
	SQLCHAR SqlState[7], Msg[SQL_MAX_MESSAGE_LENGTH];
	SQLINTEGER NativeError;

	while((ret = SQLGetDiagRec(SQL_HANDLE_STMT, m_hStmt, i, SqlState, 
		&NativeError, Msg, sizeof(Msg), &MsgLen)) != SQL_NO_DATA)
	{
		Log(LOG_FATAL, "%s %s (%d)\n", SqlState, Msg, NativeError);
		i++;
		
		if(!strcmp((char*)SqlState, "08S01"))
		{
			// db connection closed
			m_pConn->m_bDbLinkFailure = true;
		}
	}

	if(i == 1)
	{
		m_pConn->m_bDbLinkFailure = false;
		Log(LOG_FATAL, "No SQL Error found.\n");
	}
}

int PreparedStatement::GetColumnCount()
{
	if(m_pConn->m_nOdbcCallerSP <= 0)
	{
		Log(LOG_FATAL, "PrepStmt.NumResultCols: Not executed before: %s\n", m_pszOriginalQuery);
		return -1;
	}

	SQLSMALLINT cols;
	SQLRETURN ret = SQLNumResultCols(m_hStmt, &cols);

	if(IsSqlSuccessV(ret, m_hStmt))
		return cols;

	Log(LOG_FATAL, "SQLNumResultCols error %d: %s\n", ret, m_pszOriginalQuery);
	PrintSqlError();
	return -1;
}

ISqlConn* PreparedStatement::GetSqlConnection()
{
	return m_pConn;
}

//////////////////////////////////////////////////////////////////////////////

DbConn::DbConn()
{
	m_hDBC = NULL;
	m_bDbLinkFailure = false;
	m_nOdbcCallerSP = 0;
}

void DbConn::Disconnect()
{
	if(m_hDBC != NULL)
	{
		SQLDisconnect(m_hDBC);
		SQLFreeHandle(SQL_HANDLE_DBC, m_hDBC);

		m_hDBC = NULL;
	}
}

bool DbConn::Connect()
{
	SQLCHAR szConnOut[1024];
	SQLRETURN ret, ret2;

	SC_ASSERT(m_hDBC == NULL);

	ret = SQLAllocHandle(SQL_HANDLE_DBC, s_hEnv, &m_hDBC);
	if(!IsSqlSuccess(ret))
	{
		Log(LOG_FATAL, "ODBC: DBC Handle allocation failed.\n");
		return false;
	}

	const char* pszConnStr = m_pConfigSect->GetString("ConnStr");
	if(pszConnStr == NULL)
	{
		Log(LOG_FATAL, "[%s] DB connection string (ConnStr) not found.\n", m_szName);
		return false;
	}

	Log(LOG_SYSTEM, "[%s] Connecting ODBC %s: ", m_szName, pszConnStr);

	ret = SQLDriverConnect(m_hDBC, NULL, (SQLCHAR*) pszConnStr, SQL_NTS,
		szConnOut, 1024, &ret2, SQL_DRIVER_COMPLETE);

	if(IsSqlSuccess(ret))
	{
		Log(LOG_SYSTEM, "Success\n");
		return true;
	}
	else
	{
		Log(LOG_SYSTEM, "Failed\n");

		SQLSMALLINT MsgLen, i = 1;
		SQLRETURN ret;
		SQLCHAR SqlState[7], Msg[SQL_MAX_MESSAGE_LENGTH];
		SQLINTEGER NativeError;

		while ((ret = SQLGetDiagRec(SQL_HANDLE_DBC, m_hDBC, i, SqlState,
			&NativeError, Msg, sizeof(Msg), &MsgLen)) != SQL_NO_DATA)
		{
			Log(LOG_FATAL, "%s %s (%d)\n", SqlState, Msg, NativeError);
			i++;
		}

		return false;
	}
}

bool DbConn::RestoreConnection()
{
	SC_ASSERT(m_bDbLinkFailure);
	Log(LOG_DATE|LOG_FATAL, "[%s] ODBC Link failure detected. Try to restore all ODBC handles...\n", m_szName);

	std::set<PreparedStatement*>::iterator itr = m_prepstmts.begin();
	for(; itr != m_prepstmts.end(); ++itr)
	{
		PreparedStatement* pStmt = *itr;
		pStmt->FreeHandle();
	}

	Disconnect();

	if(Connect())
	{
		bool bAllOK = true;

		itr = m_prepstmts.begin();
		for(; itr != m_prepstmts.end(); ++itr)
		{
			PreparedStatement* pStmt = *itr;
			if(false == pStmt->Restore())
				bAllOK = false;
		}

		if(bAllOK)
		{
			m_bDbLinkFailure = false;
			Log(LOG_DATE|LOG_SYSTEM, "ODBC Restore Successful.\n");
			return true;
		}

		Log(LOG_DATE|LOG_ERROR, "ODBC Restore failed (Some statements preparation failed)\n");
	}
	else
	{
		Log(LOG_DATE|LOG_ERROR, "ODBC Restore failed (Couldn't connect)\n");
	}

	return false;
}

void DbConn::ErasePreparedStatement(PreparedStatement* pStmt)
{
	std::set<PreparedStatement*>::iterator itr = m_prepstmts.find(pStmt);
	if(itr != m_prepstmts.end())
	{
		delete *itr;
		m_prepstmts.erase(itr);
		return;
	}

	Log(LOG_FATAL, "Can't destroy SQL statement; invalid argument (not in set)\n");
}

bool DbConn::CreateStatement(const char* pszQueryName, ISqlStmt** ppOut, bool bIsSelectQuery)
{
	*ppOut = &s_fakeStmt;

	const char* pszOrigQuery = m_pConfigSect->GetString(pszQueryName);
	if(pszOrigQuery == NULL)
	{
		Log(LOG_FATAL, "[%s] Query '%s' not found.\n", m_szName, pszQueryName);
		g_core.m_bDbErrorOccur = true;
		return false;
	}

	PreparedStatement* pStmt = new PreparedStatement(this, pszOrigQuery, bIsSelectQuery);
	m_prepstmts.insert(pStmt);	
	*ppOut = pStmt;

	return true;
}

bool DbConn::ExecuteSql(const char* pszNonQuerySQL)
{
	SQLRETURN ret;
	SQLHSTMT hStmt;

	ret = SQLAllocHandle(SQL_HANDLE_STMT, m_hDBC, &hStmt);
	if(IsSqlSuccess(ret))
	{
		ret = SQLExecDirect(hStmt, (SQLCHAR*) pszNonQuerySQL, 
			(SQLINTEGER) strlen(pszNonQuerySQL));

		SQLFreeStmt(hStmt, SQL_CLOSE);
		SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

		return IsSqlSuccess(ret);
	}

	return false;
}

bool DbConn::Commit()
{
	SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, m_hDBC, SQL_COMMIT);
	if(!IsSqlSuccess(ret))
	{
#ifdef _DEBUG
//		PrintSqlError();
#endif
		return false;
	}

	return true;
}

void DbConn::Rollback()
{
#ifdef _DEBUG
	SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, m_hDBC, SQL_ROLLBACK);
//	if(!IsSqlSuccess(ret))
//		PrintSqlError();
#else
	SQLEndTran(SQL_HANDLE_DBC, m_hDBC, SQL_ROLLBACK);
#endif
}

bool DbConn::IsConnected()
{
	return (m_hDBC != NULL && m_bDbLinkFailure == false);
}

//////////////////////////////////////////////////////////////////////////////

ISqlConn* ServerCore::GetSqlConnection(const char* pszConnName)
{
	if(m_dbconns.empty())
	{
		Log(LOG_FATAL, "No database available. (not specified in config)\n");
		m_bDbErrorOccur = true;
		return NULL;//&s_fakeConn;
	}

	if(pszConnName == NULL)
		return m_dbconns.at(0);

	for(size_t d=0; d<m_dbconns.size(); d++)
	{
		DbConn* pConn = m_dbconns.at(d);
		if(_stricmp(pConn->m_szName, pszConnName) == 0)
		{
			return pConn;
		}
	}

	Log(LOG_FATAL, "GetSqlConnection: database '%s' not found.\n", pszConnName);
	m_bDbErrorOccur = true;
	return NULL;//&s_fakeConn;
}

bool ServerCore::BindDbStatementParams()
{
	for(size_t d=0; d<m_dbconns.size(); d++)
	{
		DbConn* pConn = m_dbconns.at(d);
		if(pConn->m_prepstmts.empty())
			continue;

		std::set<PreparedStatement*>::iterator itr = pConn->m_prepstmts.begin();
		for(; itr != pConn->m_prepstmts.end(); ++itr)
		{
			PreparedStatement* pStmt = *itr;
			if(!pStmt->BindNow())
				return false;
		}
	}
	
	return true;
}

void ServerCore::CloseDbStatements()
{
	for(size_t d=0; d<m_dbconns.size(); d++)
	{
		DbConn* pConn = m_dbconns.at(d);
		if(pConn->m_prepstmts.empty())
			continue;

		Log(LOG_SYSTEM, "[%s] Destroying %d prepared SQL statement(s).\n",
			pConn->m_szName, pConn->m_prepstmts.size());

		std::set<PreparedStatement*>::iterator itr = pConn->m_prepstmts.begin();
		for(; itr != pConn->m_prepstmts.end(); ++itr)
		{
			PreparedStatement* pStmt = *itr;
			pStmt->FreeHandle();
			delete pStmt;
		}

		pConn->m_prepstmts.clear();
	}
}

void ServerCore::CloseDatabase()
{
	for(size_t d=0; d<m_dbconns.size(); d++)
	{
		DbConn* pConn = m_dbconns.at(d);
		pConn->Disconnect();
		delete pConn;
	}

	m_dbconns.clear();

	if(s_hEnv != NULL)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, s_hEnv);
		s_hEnv = NULL;
	}
}

void ServerCore::OnInvalidDbApiCall()
{
	if(g_bServerRunning)
	{
		Shutdown();
	}
	else
	{
		m_bDbErrorOccur = true;
	}
}

bool ServerCore::OpenDatabase()
{
	const char* pszDBs;
	const char* pszName;
	int len;
	SQLRETURN ret;

	SC_ASSERT(s_hEnv == NULL);

	pszDBs = g_pCoreConfigSection->GetString("Databases");
	if(pszDBs == NULL)
	{
		// database not specified
		return true;
	}

	StringSplitter ssp(pszDBs);
	while(ssp.GetNext(&pszName, &len))
	{
		if(len >= MAX_ODBC_CONN_NAMELEN)
		{
			const_cast<char*>(pszName)[len] = 0;
			Log(LOG_FATAL, "Too long Database section name: %s\n", pszName);
			return false;
		}

		DbConn* pConn = new DbConn();
		StringCchCopyNA(pConn->m_szName, MAX_ODBC_CONN_NAMELEN, pszName, len);
		pConn->m_szName[len] = 0;
		m_dbconns.push_back(pConn);

		pConn->m_pConfigSect = GetConfigSection(pConn->m_szName);
		if(pConn->m_pConfigSect == NULL)
		{
			Log(LOG_FATAL, "Config section for database [%s] not found.\n", pConn->m_szName);
			return false;
		}
	}

	if(m_dbconns.empty())
	{
		Log(LOG_FATAL, "No database specified.\n");
		return false;
	}

	ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &s_hEnv);
	if(!IsSqlSuccess(ret))
	{
		Log(LOG_FATAL, "ODBC: Env Handle allocation failed.\n");
		return false;
	}

	ret = SQLSetEnvAttr(s_hEnv, SQL_ATTR_ODBC_VERSION, (void*) SQL_OV_ODBC3, 0);
	if(!IsSqlSuccess(ret))
	{
		Log(LOG_FATAL, "ODBC: SQLSetEnvAttr failed\n");
		return false;
	}

	for(size_t d=0; d<m_dbconns.size(); d++)
	{
		if(!m_dbconns.at(d)->Connect())
			return false;
	}

	return true;
}

#endif	// SC_DISABLE_ODBC

SC_NAMESPACE_END
