#pragma once

#if defined(_MSC_VER) && (_MSC_VER >= 1100) && defined(__cplusplus)
#	define SC_DECLSPEC_NOVTABLE __declspec(novtable)
#else
#	define SC_DECLSPEC_NOVTABLE
#endif

#define SC_DECLARE_INTERFACE(_iface_)		struct SC_DECLSPEC_NOVTABLE _iface_

#ifdef _MSC_VER
#	define SC_DECLSPEC_EXPORT __declspec(dllexport)
#elif defined(__MINGW32__)
#	define SC_DECLSPEC_EXPORT __attribute__((dllexport))
#elif defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 3)))
#	define SC_DECLSPEC_EXPORT __attribute__((visibility ("default")))
#else
#	define SC_DECLSPEC_EXPORT
#endif

#ifdef _MSC_VER
#define SC_APIENTRY		__stdcall
#else
#define SC_APIENTRY
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1300)
#	define SC_DECLSPEC_ALIGN(x) __declspec(align(x))
#elif defined(__GNUC__)
#	define SC_DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
#else
#	define SC_DECLSPEC_ALIGN(x)
#endif

#if defined(_MSC_VER)
#	define SC_DECLSPEC_THREAD __declspec(thread)
#else
#	define SC_DECLSPEC_THREAD __thread
#endif

#if defined(__x86_64) || defined(__x86_64__) || defined(__ia64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_IA64) || defined(_WIN64)
#	define SC_64BIT
#endif

#define SC_NAMESPACE_BEGIN	namespace svrcore {
#define SC_NAMESPACE_END	}

SC_NAMESPACE_BEGIN

#ifdef _WIN32
//typedef LONG ATOMIC32;
//typedef __int64 ATOMIC64;
#define ATOMIC32	LONG
#define ATOMIC64	__int64
#else
typedef int32_t ATOMIC32;
typedef int64_t ATOMIC64;
#endif

SC_DECLARE_INTERFACE(ISocketStreamFilter)
{
	virtual void FilterNetworkStream(unsigned char* pDstFiltered, const unsigned char* pSrcRaw, int len) = 0;
};

SC_DECLARE_INTERFACE(ITcpSocket)
{
	virtual void Send(const void* pData, int len) = 0;
	virtual void Kick(bool bCloseNow = false) = 0;
	virtual int GetPendingBytesToSend() = 0;
	virtual void SetCalledOnSendBufferEmpty(bool bWantCalledOnEmpty) = 0;	// default is false: not called on empty send buffer
	virtual void RequestReceiveBuffer() = 0;	// OnReceive() will be called although no actual network event. (NOTE: ignored if nLength is 0)
	virtual void SetSendFilter(ISocketStreamFilter* pFilter) = 0;
	virtual const char* GetRemoteAddr(unsigned int* pIpAddr = NULL, unsigned short *pPortNum = NULL) = 0;
	virtual void GetLocalAddr(unsigned int *pIpAddr, unsigned short *pPortNum = NULL) = 0;
};

SC_DECLARE_INTERFACE(ITcpSocketListener)
{
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength) = 0;
	virtual void OnSendBufferEmpty() = 0;
	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode) = 0;	// on error, pSocket is NULL
	virtual void OnDisconnect() = 0;
};

struct SockAddrT
{
	char opaque[16];
};

SC_DECLARE_INTERFACE(IUdpSocket)
{
	virtual bool Send(const SockAddrT* pTo, const void* pData, int len) = 0;
	virtual void GetLocalAddr(unsigned int *pIpAddr, unsigned short *pPortNum = NULL) = 0;
	virtual void MakeRemoteAddr(unsigned int nRemoteIpAddr, unsigned short nRemotePort, SockAddrT *pOut) = 0;
	virtual void TranslateAddr(const SockAddrT* pSrc, unsigned int *pIpAddr, unsigned short *pPortNum) = 0;
};

SC_DECLARE_INTERFACE(IUdpSocketListener)
{
	virtual void OnReceive(const SockAddrT* pFrom, char* pBuffer, unsigned int nLength) = 0;
	virtual void OnSocketCreated(IUdpSocket* pSocket, int nErrorCode) = 0;
};

// Generally, user shall not call IRefCounted member methods; just implement.
SC_DECLARE_INTERFACE(IRefCounted)
{
	virtual void Ref_Release() = 0;
	virtual ATOMIC32 Ref_Retain() = 0;
	virtual ATOMIC32 Ref_GetCount() = 0;		// logic checking purpose (not used importantly)
};

// Client listener is similar with Socket listener but can listen to Life-Cycle events additionally.
SC_DECLARE_INTERFACE(ITcpClientListener) : public virtual IRefCounted, public ITcpSocketListener
{
	virtual void OnRelease() =0;	// called after OnDisconnect and before going to the pool
	virtual void OnFinalDestruct() =0;	// called before destroying the pool
};

// helper class to forget all about IRefCounted implementation
class BaseClientListener : public ITcpClientListener
{
	ATOMIC32 m_nReferenceCount;
public:
	BaseClientListener() { m_nReferenceCount = 0; }
	virtual ATOMIC32 Ref_GetCount() { return m_nReferenceCount; }
	virtual ATOMIC32 Ref_Retain() { return _InterlockedIncrement(&m_nReferenceCount); }
	virtual void Ref_Release() 
	{
		if(0 == _InterlockedDecrement(&m_nReferenceCount)) 
			OnRelease();
	}
};

// The framework calls OnTimer method if the object's refcnt >= 1
SC_DECLARE_INTERFACE(ITimerListener) : public virtual IRefCounted
{
	virtual void OnTimer(int nTimerID) =0;
};

// if a derived class needs both ITcpClientListener and ITimerListener, use BaseClientListenerWithTimer
class BaseClientListenerWithTimer : public ITcpClientListener, public ITimerListener
{
	ATOMIC32 m_nReferenceCount;
public:
	BaseClientListenerWithTimer() { m_nReferenceCount = 0; }
	virtual ATOMIC32 Ref_GetCount() { return m_nReferenceCount; }
	virtual ATOMIC32 Ref_Retain() { return _InterlockedIncrement(&m_nReferenceCount); }
	virtual void Ref_Release()
	{
		if(0 == _InterlockedDecrement(&m_nReferenceCount)) 
			OnRelease();
	}
};

SC_DECLARE_INTERFACE(IJobThreadListener)
{
	virtual void OnDispatch(int nJobID, int nParam, void* pParam) =0;
	virtual bool OnCreate() =0;		// called on new thread after spawn
	virtual void OnDestroy() =0;	// called before the thread exits
};

SC_DECLARE_INTERFACE(IJobQueue)
{
	// if pSender != NULL, the refcnt will increase then decrease after OnDispatch done.
	virtual void Post(IRefCounted* pSender, int nJobID, int nParam, void* pParam) =0;
	virtual int GetPendingJobCount() =0;
	virtual void AddMoreThread(IJobThreadListener* pJTL) =0;
};


// ODBC DATE_STRUCT
struct SqlDateT
{
	short year;
	unsigned short month;
	unsigned short day;
};

// ODBC TIME_STRUCT
struct SqlTimeT
{
	unsigned short hour;
	unsigned short minute;
	unsigned short second;
};

// ODBC TIMESTAMP_STRUCT
struct SqlDateTimeT
{
	short year;
	unsigned short month;
	unsigned short day;
	unsigned short hour;
	unsigned short minute;
	unsigned short second;
	unsigned int fraction;
};

struct ISqlConn;
SC_DECLARE_INTERFACE(ISqlStmt)
{
	virtual bool Execute(OUT int* pAffectedRowCount =NULL) =0;
	virtual bool Fetch() =0;
	virtual void Close() =0;
	virtual void Destroy() =0;

	virtual ISqlConn* GetSqlConnection() =0;
	virtual int GetColumnCount() =0;
	virtual bool NextResult() =0;

	virtual void BindParam(const char *name, short* param) =0;
	virtual void BindParam(const char *name, int* param) =0;
	virtual void BindParam(const char *name, __int64* param) =0;
	virtual void BindParam(const char *name, float* param) =0;
	virtual void BindParam(const char *name, char* param, int len) =0;
	virtual void BindParam(const char *name, WCHAR* param, int len) =0;
	virtual void BindParam(const char *name, SqlDateT* param) =0;
	virtual void BindParam(const char *name, SqlTimeT* param) =0;
	virtual void BindParam(const char *name, SqlDateTimeT* param) =0;

	virtual void BindColumn(const char *name, short *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, int *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, __int64 *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, float *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, char* outbuf, int buflen, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, WCHAR* outbuf, int buflen, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, SqlDateT* outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, SqlTimeT* outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(const char *name, SqlDateTimeT* outbuf, int* pLenOrInd =NULL) =0;

	// first column index is 1 (not 0!)
	virtual void BindColumn(int nColumnIndex, short *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, int *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, __int64 *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, float *outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, char* outbuf, int buflen, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, WCHAR* outbuf, int buflen, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, SqlDateT* outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, SqlTimeT* outbuf, int* pLenOrInd =NULL) =0;
	virtual void BindColumn(int nColumnIndex, SqlDateTimeT* outbuf, int* pLenOrInd =NULL) =0;
};

SC_DECLARE_INTERFACE(ISqlConn)
{
	virtual bool Commit() =0;
	virtual void Rollback() =0;
	virtual bool CreateStatement(const char* pszQueryName, ISqlStmt** ppOut, bool bIsSelectQuery) =0;
	virtual bool ExecuteSql(const char* pszNonSelectQuerySQL) =0;
	virtual bool IsConnected() =0;
};

SC_DECLARE_INTERFACE(IConfigSection)
{
	virtual const char* GetString(const char* key) =0;
	virtual int GetInteger(const char* key, int nDefault =0) =0;
	virtual bool GetBoolean(const char* key, bool bDefault =false) =0;
	virtual float GetFloat(const char* key, float fDefault =0) =0;
	virtual const char* GetFilename(const char* key) =0;

	virtual int GetKeyCount() =0;
	virtual bool GetPair(int iKey, const char** ppKeyOut, const char** ppValueOut) =0;
};

SC_DECLARE_INTERFACE(IServerStateQuery)
{
	virtual int GetFreeSocketCount() =0;
	virtual int GetUseSocketCount() =0;

	// valid only when the server did not start listening:
	virtual void SetListenAddr(unsigned int nIpAddr) =0;
	virtual void SetListenPort(unsigned short nPort) =0;
};

SC_DECLARE_INTERFACE(IGlobalStateQuery)
{
	virtual int GetCpuLoad(char aPercentages[]) =0;
	virtual int GetMemoryLoad() =0;
};

SC_DECLARE_INTERFACE(ILogger)
{
	virtual void LogMessage(int& loguid, int level, const char* format, ...) = 0;
	virtual void LogMessage(int& loguid, int level, const WCHAR* format, ...) = 0;
};

typedef ITcpClientListener* (*ClientListenerCreatorFuncT)(const void*);

SC_DECLARE_INTERFACE(IServerCore)
{
	virtual void RegisterClientListener(ClientListenerCreatorFuncT pfn, const void* param =NULL, const char* pszTargetSvr =NULL) =0;
	virtual void RegisterUdpListener(IUdpSocketListener* pListener, const char* pszTargetSvr =NULL) =0;
	virtual bool Connect(ITcpSocketListener* pListener, const char* pszAddress, unsigned short nRemotePort, unsigned short nLocalPort =0) =0;
	virtual void SetTimer(ITimerListener* pListener, int nTimerID, int nTimeoutMS, bool bRepeat) =0;
	virtual void ClearTimer(ITimerListener* pListener, int nTimerID =0) =0;
	virtual IConfigSection* GetConfigSection(const char* pszName) =0;
	virtual IJobQueue* CreateJobThread(IJobThreadListener* pJTL) =0;
	virtual ISqlConn* GetSqlConnection(const char* pszConnName =NULL) =0;
	virtual IServerStateQuery* GetServerStateQuery(const char* pszName =NULL) =0;
	virtual IGlobalStateQuery* GetGlobalStateQuery() =0;
	virtual bool StartListen() =0;	// only effective when "AutoStart=false" is specified in config
	virtual void Shutdown() =0;
	//virtual ScResultCodes GetSubSystem(SC_INTERFACE_IDTYPE iid, OUT void** ppOut) =0;
};

//////////////////////////////////////////////////////////////////////////////
// ServerSide Framework Helpers:

#define SC_FRAMEWORK_MODULE_VERSION		0x0103

struct IServerModule
{
	virtual int GetVersion() { return SC_FRAMEWORK_MODULE_VERSION; }
	virtual bool Create(IServerCore* pCore) =0;
	virtual void Destroy() =0;
};

struct HazardPtrT
{
	HazardPtrT* __notouch_HazardPtrNext;
	ATOMIC32 __notouch_PtrIsActive;
	void* ptr;
};

typedef void (*HazardReclaimFuncT)(void* p);

struct ServerSideProxyT
{
	ILogger* pLogger;
	//Memory::IMemoryPool* pMemPool;

	int (*pfnGetThreadIndex)();		// returns unique seqential thread index from 0(=main)

	// High performance multi-threaded memory allocator
	void (*pfnMemFree)(void *mem);
	void* (*pfnMemAlloc)(size_t size);
	void* (*pfnMemRealloc)(void *mem, size_t size);
	void (*pfnAlignedFree)(void *mem);
	void* (*pfnAlignedAlloc)(size_t size, size_t alignment);	// be careful, size is first param

	// Hazard Pointer implementation
	// http://www.research.ibm.com/people/m/michael/ieeetpds-2004.pdf
	HazardPtrT* (*pfnHazardAcquire)(void** pp);
	void (*pfnHazardRelease)(HazardPtrT* p);
	void (*pfnHazardRetire)(void* ptrToFree, HazardReclaimFuncT pfnReclaim);

	void (*pfnAssert)(const char* eval, const char* file, int line);
};

extern ServerSideProxyT __svrsideProxy;

SC_NAMESPACE_END


// log level
#define LOG_VERBOSE			0
#define LOG_DEBUG			1
#define LOG_INFO			2
#define LOG_WARN			3
#define LOG_ERROR			4
#define LOG_FATAL			5
#define LOG_SYSTEM			6

#define MAX_LOG_LEVEL		8

#define LOG_DATE			0x80	// For console log, add datetime prefix

#define __VarNameCat(x,y) x##y
#define _VarNameCat(x,y) __VarNameCat(x,y)
#define UidVarName(x) _VarNameCat(x,__LINE__)

#define Log(_level,...) {static int UidVarName(_LogUid_)=0; __svrsideProxy.pLogger->LogMessage(UidVarName(_LogUid_),_level,__VA_ARGS__);}

#define ScGetThreadIndex (*__svrsideProxy.pfnGetThreadIndex)
/*
#define ScMemFree (__svrsideProxy.pfnMemFree)
#define ScMemAlloc (__svrsideProxy.pfnMemAlloc)
#define ScMemRealloc (__svrsideProxy.pfnMemRealloc)
#define ScAlignedFree (__svrsideProxy.pfnAlignedFree)
#define ScAlignedAlloc (__svrsideProxy.pfnAlignedAlloc)
#define ScPoolFree (__svrsideProxy.pMemPool->Free)
#define ScPoolAlloc (__svrsideProxy.pMemPool->Alloc)
*/
#define ScHazardAcquire(_pp) (__svrsideProxy.pfnHazardAcquire)((void**)_pp)
#define ScHazardRelease (__svrsideProxy.pfnHazardRelease)
#define ScHazardRetire (__svrsideProxy.pfnHazardRetire)

#if defined(_DEBUG) || defined(SC_FORCE_ENABLE_ASSERT)
	#define SC_ASSERT(f)   (void)( (!!(f)) || ((__svrsideProxy.pfnAssert)(#f, __FILE__, __LINE__), 0) )
	#define SC_VERIFY      SC_ASSERT
#else
	#define SC_ASSERT(f)   (void)(0)
	#define SC_VERIFY(f)   ((void)(f))
#endif

#define SC_SERVERMODULE_ENTRY(__module_class_name)\
	svrcore::ServerSideProxyT svrcore::__svrsideProxy; \
	extern "C" SC_DECLSPEC_EXPORT IServerModule* ScModuleEntry(const svrcore::ServerSideProxyT* p)\
	{ static __module_class_name entry; __svrsideProxy = *p; return &entry; }
