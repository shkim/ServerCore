#pragma once

#include "jobthr.h"
#include "pathfilename.h"
#include "socket.h"

#ifndef SC_DISABLE_ODBC
#include "odbc.h"
#endif

SC_NAMESPACE_BEGIN

#define MAX_THREADS_LIMIT			16

class ServerPart;

class ConfigSection : public IConfigSection
{
public:
	virtual const char* GetString(const char* name);
	virtual int GetInteger(const char* name, int nDefault);
	virtual bool GetBoolean(const char* name, bool bDefault);
	virtual float GetFloat(const char* key, float fDefault);
	virtual const char* GetFilename(const char* name);

	virtual int GetKeyCount();
	virtual bool GetPair(int iKey, const char** ppKeyOut, const char** ppValueOut);

	void Add(const char* pszName, const char* pszValue);

private:
	const char* Find(const char* name);

	struct TPair
	{
		const char* pszName;
		const char* pszValue;
	};

	std::vector<TPair> m_pairs;
};

class ConfigFile
{
public:
	ConfigFile();
	~ConfigFile();

	bool Load(LPCTSTR pszFilename);
	IConfigSection* GetSection(const char* name);

	inline const char* GetConfigDir() { return m_path.GetCurrentDir(); }

private:
	bool ParseConfFile();
	ConfigSection* OpenNewSection(const char* pszName);

	char* m_pRawFile;

	struct TSection
	{
		const char* pszName;
		ConfigSection* pSection;
	};

	PathFilenamer m_path;
	std::vector<TSection> m_sections;

	friend class ConfigSection;
};

//////////////////////////////////////////////////////////////////////////////

struct ModuleFileInfoT
{
	HMODULE hModule;
	IServerModule* pModCtrl;
	ServerPart* pPart;
	bool bCreated;
};

struct TTimerItem
{
	LARGE_INTEGER nExpireTime;
	inline bool operator<(const TTimerItem& b) const
		{ return nExpireTime.QuadPart < b.nExpireTime.QuadPart; }

	__int64 nDefaultTimeout;
	ITimerListener* pListener;
	int nTimerID;
	bool bRepeat;
};

// ServerPart is a per-LISTEN-socket object.
class ServerPart : public IServerStateQuery
#ifdef SC_PLATFORM_POSIX
	,public IoEventListener
#endif
{
public:
	ServerPart(const char* pszName, int lenName);
#ifdef _DEBUG
	~ServerPart() { SC_ASSERT(m_hListenSocket == INVALID_SOCKET); }
#endif

	virtual int GetFreeSocketCount();
	virtual int GetUseSocketCount();

	virtual void SetListenAddr(unsigned int nIpAddr);
	virtual void SetListenPort(unsigned short nPort);

#ifdef SC_PLATFORM_POSIX
	virtual void IO_OnReadable();
	virtual void IO_OnWritable();
	virtual void IO_OnException();
	void OnTcpSocketClosed(TcpSocket* pSocket);
#else
	void DiscardMalfunctionSocket(TcpSocket* pSocket);
#endif

	bool CreateListenSocket();
	bool CreateAcceptSockets();
	void PrecrateMoreSockets(int nPrecreateCount);
	bool CreateUdpSocket();

	void KickAcceptSockets();
	void CloseAcceptSockets();

	void Destroy();

	enum
	{
		BIND_TCP =1,
		BIND_UDP =2
	};

	inline bool IsTCP() const { return (m_nServerBindFlags & BIND_TCP) != 0; }
	inline bool IsUDP() const { return (m_nServerBindFlags & BIND_UDP) != 0; }

	int m_nMaxSendBuffersPerConn;

	ATOMIC64 m_nTotalRecvBytes;
	ATOMIC64 m_nTotalSentBytes;

	ATOMIC32 m_nConcurrentConn;

	int m_nMaxSockets;
	int m_nCurCreatedSockets;
	int m_nPrecreateSockets;
	int m_nMaxAcceptSocketLimit;	// m_nMaxSockets + m_nPrecreateSockets

	int m_nListenerTypeID;
	SOCKET m_hListenSocket;

	UdpSocket* m_pUdpSocket;
	IUdpSocketListener* m_pUdpSocketListener;

	const char* m_pszListenAddress;
	unsigned short m_nListenPort;
	char m_szName[48], m_szOverrideListenAddr[16];
	BYTE m_nServerBindFlags;	// TCP,UDP
	
#ifndef SC_PLATFORM_POSIX
	std::vector<TcpSocket*> m_AcceptSockets;
#else
	std::list<TcpSocket*> m_BusySockets;
#endif

	CriticalSection m_PartLock;

	friend class ServerCore;
};

class ServerCore : public IServerCore, public IGlobalStateQuery
{
public:
	ServerCore();
	~ServerCore();

	virtual void RegisterClientListener(ClientListenerCreatorFuncT pfn, const void* param, const char* pszTargetSvr);
	virtual void RegisterUdpListener(IUdpSocketListener* pListener, const char* pszTargetSvr);
	virtual bool Connect(ITcpSocketListener* pListener, const char* pszAddress, unsigned short nRemotePort, unsigned short nLocalPort);
	virtual void SetTimer(ITimerListener* pListener, int nTimerID, int nTimeoutMS, bool bRepeat);
	virtual void ClearTimer(ITimerListener* pListener, int nTimerID =0);
	virtual IConfigSection* GetConfigSection(const char* pszName);
	virtual IJobQueue* CreateJobThread(IJobThreadListener* pJTL);
	virtual ISqlConn* GetSqlConnection(const char* pszConnName);
	virtual IServerStateQuery* GetServerStateQuery(const char* pszName);
	virtual IGlobalStateQuery* GetGlobalStateQuery();
	virtual bool StartListen();
	virtual void Shutdown();
	//virtual ScResultCodes GetSubSystem(SC_INTERFACE_IDTYPE iid, OUT void** ppOut) { return SC_UNKNOWN_INTERFACE; }

	virtual int GetMemoryLoad();
	virtual int GetCpuLoad(char aUsage[]);

	void EnterMainLoop();

	bool LoadModules(IConfigSection* pSvrSect);
	bool InitializeModules();
	void FreeModules();
	ServerPart* FindRegTargetSvrPart(const char*& pszTargetSvr);

	bool CreateIOCP();
	void CloseIOCP();
	bool ReadServerPartInfo(IConfigSection* pSvrSect);
	bool CreateServerParts();

	bool CreateNetworkerThreads(int nThreadCnt);
	void TerminateNetworkThreads();
	void DiscardConnectSocket(TcpSocket* pSocket);
	void KickAllSockets();
	void CloseAcceptSockets();
	void CloseConnectSockets();
	void DestroySockets();
	
#ifdef SC_PLATFORM_POSIX
	TcpSocket* GetFreeAcceptSocket();
	void DiscardAcceptSocket(TcpSocket* pSocket);
#endif

	bool SpawnJobThreads();
	void TerminateJobThreads();
	void DestroyJobQueues();

	bool OpenDatabase();
	void CloseDatabase();
	bool BindDbStatementParams();
	void CloseDbStatements();
	void OnInvalidDbApiCall();

	void Main();

	inline int GetNetworkThreadCount() const { return m_nWorkThreadCount; }

	bool m_bDbErrorOccur;
//	bool m_bDbApiErrorOccur;
	int m_nMaxSendBuffersPerConn;	// for connect socket

private:
	void CreateMainMonitorEvents();
	DWORD ReconfigureTimers();
	DWORD OnTimeout();
	DWORD _InsertTimerJobs(std::priority_queue<TTimerItem>& addjobs);
	void OnServerShutdown();

//	int m_nInitialThread;
//	LONG m_nThreadsCurrent;	// current iocp worker threads count
//	LONG m_nThreadsBusy;	// busy worker threads count

	CriticalSection m_CoreLock;
	CriticalSection m_csTimer;

	// socket i/o (IOCP) threads (not job worker threads)
	int m_nWorkThreadCount;
	THREAD_HANDLE m_aWorkThreadHandles[MAX_THREADS_LIMIT];
	UINT m_aWorkThreadIDs[MAX_THREADS_LIMIT];

	// queued job threads
	int m_nJobThreadCount;
	int m_nJobQueueCount;
	JobQueue* m_pJobQueues;

	std::vector<TcpSocket*> m_FreeConnectSockets;
	std::list<TcpSocket*> m_BusyConnectSockets;
	std::list<UdpSocket*> m_BusyUdpSockets;
#ifndef SC_DISABLE_ODBC
	std::vector<DbConn*> m_dbconns;
#endif

	std::vector<ServerPart*> m_SvrParts;
	std::list<TTimerItem> m_listTimers;
	std::vector<TTimerItem> m_TimerReconfJobs;

#ifdef SC_PLATFORM_POSIX
	std::vector<TcpSocket*> m_FreeAcceptSockets;
	friend class CompletedIoQueue;
#else
	HANDLE m_aMainMonitorEvents[2];	// 0:timer-reconfigure 1:shutdown
#endif

	std::vector<ModuleFileInfoT> m_modules;
};

//////////////////////////////////////////////////////////////////////////////

class StringSplitter
{
public:
	StringSplitter(const char* pszInput, const char* pszDelimiters =NULL);
	void Reset(const char* pszInput, const char* pszDelimiters =NULL);
	bool GetNext(const char** ppszSplitted, int* pLength);

private:
	bool IsDelimiterChar(const char ch);

	const char* m_pCurrent;
	const char* m_pEnd;
	const char* m_pszDelimiters;
};

class LineDispenser
{
public:
	LineDispenser(const char* pszInput);
	bool GetNext(const char** ppszLine, int* pLength);

private:
	const char* m_pCurrent;
	const char* m_pEnd;
};

char *TrimString(char *psz);
bool IsDebuggerAttached();
void SetMyThreadIndex();
void Hazard_OnThreadExit();

extern bool g_bServerRunning;
extern bool g_bServerStartedListening;
extern ConfigFile* g_pConfFile;
extern ServerCore g_core;
extern ClientListenerPool g_clipool;
extern GrowOnlyAllocList g_poolAllocs;
extern HANDLE g_hCompPort;
extern LPCTSTR g_pszConfigIniFile;
extern LPCTSTR g_pszServiceName;
extern int g_nSendBufferSize;

//////////////////////////////////////////////////////////////////////////////

bool MakeSocketReuseAddr(SOCKET hSocket, const char* desc);
const char* GetWsaErrorString(int nErrorCode);	// returns "WSA???" string
void EventLog_Error(const TCHAR* format, ...);	// To EventLog on Service mode, To console on normal mode

#ifdef _WIN32
bool InitWinsock();
bool GetExtSockFuncs();
extern HANDLE g_hUdpWorkThread;

//void ScAlignedFree(void *mem);
//void* ScAlignedAlloc(size_t size, size_t alignment);		// FIRST parameter is SIZE!
#define ScAlignedFree		_aligned_free
#define ScAlignedAlloc		_aligned_malloc

#if _WIN32_WINNT <= 0x0501
__int64 _InterlockedExchangeAdd64(__int64 volatile * Addend, __int64 Value);
#endif

#else	// _WIN32
BOOL PathAppend(LPTSTR pszPath, LPCTSTR pszMore);
DWORD GetTickCount();

#endif

#ifndef SC_PLATFORM_POSIX

void ReportServiceStarted();

// IOCP helper (from Programming Server-Side Applications for Windows)
HANDLE CreateNewCompletionPort(DWORD dwNumberOfConcurrentThreads);
BOOL AssociateDeviceWithCompletionPort(HANDLE hCompPort, HANDLE hDevice, ULONG_PTR nCompKey);
void CreateCrashDump(EXCEPTION_POINTERS* pep);

extern LPFN_ACCEPTEX g_lpfnAcceptEx;
extern LPFN_GETACCEPTEXSOCKADDRS g_lpfnGetAcceptExSockaddrs;
extern LPFN_TRANSMITFILE g_lpfnTransmitFile;
extern LPFN_CONNECTEX g_lpfnConnectEx;
extern LPFN_DISCONNECTEX g_lpfnDisconnectEx;

#endif

SC_NAMESPACE_END
