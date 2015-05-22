#pragma once

SC_NAMESPACE_BEGIN

#define MAX_ODBC_CALLER_STACK	8
#define MAX_ODBC_CONN_NAMELEN	48

class PreparedStatement;

class DbConn : public ISqlConn
{
public:
	DbConn();
#ifdef _DEBUG
	~DbConn() { SC_ASSERT(m_hDBC == NULL && m_prepstmts.empty()); }
#endif

	bool Connect();
	void Disconnect();
	bool RestoreConnection();
	void ErasePreparedStatement(PreparedStatement* pStmt);

	virtual bool Commit();
	virtual void Rollback();
	virtual bool CreateStatement(const char* pszQueryName, ISqlStmt** ppOut, bool bIsSelectQuery);
	virtual bool ExecuteSql(const char* pszNonQuerySQL);
	virtual bool IsConnected();

private:
	SQLHDBC m_hDBC;
	bool m_bDbLinkFailure;
//	bool m_bDbErrorOccur;
	std::set<PreparedStatement*> m_prepstmts;
	IConfigSection* m_pConfigSect;
	char m_szName[MAX_ODBC_CONN_NAMELEN];

	struct OdbcCaller
	{
		DWORD nThreadID;
		PreparedStatement* pStmt;
	};

	OdbcCaller m_OdbcCallerStack[MAX_ODBC_CALLER_STACK];
	int m_nOdbcCallerSP;

	friend class PreparedStatement;
	friend class ServerCore;
};

class PreparedStatement : public ISqlStmt
{
public:
	PreparedStatement(DbConn* pDbConn, const char* pszOrigQuery, bool bIsSelectQuery);
	~PreparedStatement();

	void FreeHandle();
	bool Restore();
	void PrintSqlError();
	bool BindNow();

	virtual void Destroy();

	virtual bool Execute(int* pAffectedRowCount);
	virtual bool Fetch();
	virtual void Close();

	virtual int GetColumnCount();
	virtual ISqlConn* GetSqlConnection();
	virtual bool NextResult();

	virtual void BindParam(const char *name, short* param);
	virtual void BindParam(const char *name, int* param);
	virtual void BindParam(const char *name, __int64* param);
	virtual void BindParam(const char *name, float* param);
	virtual void BindParam(const char *name, char* param, int len);
	virtual void BindParam(const char *name, WCHAR* param, int len);
	virtual void BindParam(const char *name, SqlDateT* param);
	virtual void BindParam(const char *name, SqlTimeT* param);
	virtual void BindParam(const char *name, SqlDateTimeT* param);
//	virtual void BindParamBinary(const char *name, void* param, int len);

	virtual void BindColumn(const char *name, short *outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, int *outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, __int64 *outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, float *outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, char* outbuf, int buflen, int* pLenOrInd);
	virtual void BindColumn(const char *name, WCHAR* outbuf, int buflen, int* pLenOrInd);
	virtual void BindColumn(const char *name, SqlDateT* outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, SqlTimeT* outbuf, int* pLenOrInd);
	virtual void BindColumn(const char *name, SqlDateTimeT* outbuf, int* pLenOrInd);

	virtual void BindColumn(int nColumnIndex, short *outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, int *outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, __int64 *outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, float *outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, char* outbuf, int buflen, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, WCHAR* outbuf, int buflen, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, SqlDateT* outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, SqlTimeT* outbuf, int* pLenOrInd);
	virtual void BindColumn(int nColumnIndex, SqlDateTimeT* outbuf, int* pLenOrInd);

//	inline bool IsError() { return m_bErrorOccur; }

private:
	void CollectParamUserArg(const char* name, int sqltype, int ctype, void* pBuffer, int nBufferLen);
	void CollectColumnUserArg(const char* name, int ctype, void* pBuffer, int nBufferLen, int* pLenOrInd);
	void _BindColumn(int nIndex, int ctype, void* pBuffer, int nBufferLen, int* pLenOrInd);

	struct ParamBindInfo
	{
		void* ptr;
		SQLLEN sqllen;
		short sqltype;
		short ctype;
		short bufflen;
	};

	struct ParamUserArg
	{
		char* pos;
		short marker_len;

		short param_sqltype;
		short param_ctype;
		short param_length;
		void* param_ptr;
	};

	struct ColumnUserArg
	{
		union
		{
			const char* name;
			int index;
		};

		void* ptr;
		SQLLEN* pLenOrInd;

		short ctype;
		short length;
		
		bool bColumnFound;	// used for named binding
	};

	union
	{
		ParamUserArg* m_aParamUserArgs;
		ParamBindInfo* m_aParamBindInfo;
	};

	int m_nParamCount;

	SQLHSTMT m_hStmt;
	DbConn* m_pConn;
	
	//bool m_bCloseCursorOnEnd;
	bool m_bIsSelectQuery;
	bool m_bBindParamDone;
	bool m_bBindColumnDone;
	bool m_bParamArrayAdjusted;
	char m_nColumnBindType;		// 0=unknown, 1=named, 2=indexed

	std::vector<ColumnUserArg> m_vecNamedColumns;
	char* m_pszEdittedSQL;
	const char* m_pszOriginalQuery;	// from config
};

SC_NAMESPACE_END