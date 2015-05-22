#include "stdafx.h"
#include "scimpl.h"

//#include "../logger/logman.h"
//#include "../memory/memman.h"

SC_NAMESPACE_BEGIN

ServerCore g_core;
ClientListenerPool g_clipool;
GrowOnlyAllocList g_poolAllocs;
LPCTSTR g_pszConfigIniFile;
LPCTSTR g_pszServiceName;
bool g_bServerRunning = false;
bool g_bServerStartedListening = false;
ConfigFile* g_pConfFile = NULL;
HANDLE g_hCompPort = INVALID_HANDLE_VALUE;
const char* g_pszCoreConfigSectionName = "ServerCore";
IConfigSection* g_pCoreConfigSection;
ServerSideProxyT __svrsideProxy;
//LogManager g_logman;
//LfMemPool* g_pMemPool;
int g_nSendBufferSize;
static bool s_hasEverEnterMainLoop = false;

int _GetThreadIndex();
HazardPtrT* _HazardAcquire(void** pp);
void _HazardRelease(HazardPtrT* p);
void _HazardRetire(void* ptrToFree, HazardReclaimFuncT pfnReclaim);
void Hazard_OnFinalReclaimAll();
void StartNtService();
bool LogOpen();
void LogClose();


bool ServerCore::StartListen()
{
	if(g_bServerStartedListening)
	{
		Log(LOG_FATAL, "Server already started listening.\n");
		return false;
	}

	g_bServerStartedListening = true;	
	for(UINT i=0; i<m_SvrParts.size(); i++)
	{
		ServerPart* pPart = m_SvrParts.at(i);

		if(pPart->IsTCP())
		{
			if(!pPart->CreateListenSocket())
				return false;
			
			if(!pPart->CreateAcceptSockets())
				return false;
		}

		if(pPart->IsUDP())
		{
			if(!pPart->CreateUdpSocket())
				return false;
		}
	}

	return true;
}

void ServerCore::Shutdown()
{
	if(g_bServerRunning)
	{
		g_bServerRunning = false;
		OnServerShutdown();
	}
	else
	{
		Log(LOG_DATE|LOG_SYSTEM, "Shutdown fired but not processed.\n");
	}
}

void ScAssert(const char* eval, const char* file, int line);

#ifdef _DEBUG

static void* DbgAlignedAlloc(size_t size, size_t alignment)
{
	if(alignment > 128 && alignment > size)
	{
		Log(LOG_WARN, "ScAlignedAlloc(size=%d,alignment=%d): is this right?\n", size, alignment);
	}

#ifdef SC_USE_INTELTBB
	return scalable_aligned_malloc(size, alignment);
#elif defined(_WIN32)
	return _aligned_malloc(size, alignment);
#elif defined(HAVE_MEMALIGN)
	return memalign(alignment, size);
#else
	void* p;
	posix_memalign(&p, alignment, size);
	return p;
#endif
}

#else // _DEBUG

#if !defined(SC_USE_INTELTBB) && !defined(HAVE_MEMALIGN)
static inline void* _CallMemAlign(size_t size, size_t alignment)
{
#ifdef HAVE_MEMALIGN
	return memalign(alignment, size);
#elif defined(_WIN32)
	return _aligned_malloc(size, alignment);
#else
	void* p;
	posix_memalign(&p, alignment, size);
	return p;
#endif
}
#endif

#endif

void ServerCore::Main()
{
#if defined(SC_USE_INTELTBB)
	__svrsideProxy.pfnMemFree = scalable_free;
	__svrsideProxy.pfnMemAlloc = scalable_malloc;
	__svrsideProxy.pfnMemRealloc = scalable_realloc;
	__svrsideProxy.pfnAlignedFree = scalable_aligned_free;
#else
	__svrsideProxy.pfnMemFree = free;
	__svrsideProxy.pfnMemAlloc = malloc;
	__svrsideProxy.pfnMemRealloc = realloc;
	__svrsideProxy.pfnAlignedFree =
	#ifdef _WIN32
		_aligned_free;
	#else
		free;
	#endif
#endif

#ifdef _DEBUG
	__svrsideProxy.pfnAlignedAlloc = DbgAlignedAlloc;
#elif defined(SC_USE_INTELTBB)
	__svrsideProxy.pfnAlignedAlloc = scalable_aligned_malloc;
#else
	__svrsideProxy.pfnAlignedAlloc = _CallMemAlign;
#endif

	__svrsideProxy.pfnAssert = ScAssert;
	__svrsideProxy.pfnGetThreadIndex = _GetThreadIndex;

	__svrsideProxy.pfnHazardAcquire = _HazardAcquire;
	__svrsideProxy.pfnHazardRelease = _HazardRelease;
	__svrsideProxy.pfnHazardRetire = _HazardRetire;


#ifdef SC_NO_AUTOTLS
	extern TLSID_TYPE tlsId_ThreadIndex;
	extern TLSID_TYPE tlsId_HazardRetireList;
	extern TLSID_TYPE tlsId_HazardRetireListSize;
#ifdef _WIN32
	tlsId_ThreadIndex = TlsAlloc();
	tlsId_HazardRetireList = TlsAlloc();
	tlsId_HazardRetireListSize = TlsAlloc();
#else
	if(0 != pthread_key_create(&tlsId_ThreadIndex, NULL)
	|| 0 != pthread_key_create(&tlsId_HazardRetireList, NULL)
	|| 0 != pthread_key_create(&tlsId_HazardRetireListSize, NULL))
	{
		Log(LOG_FATAL, "Thread Local Storage allocation failed.\n");
		return;
	}
#endif
#endif


/*
	g_pMemPool = new LfMemPool();
	if(!g_pMemPool->Create())
	{
		Log(LOG_FATAL, "Memory pool initialization failed.\n");
		delete g_pMemPool;
		return;
	}
	__svrsideProxy.pMemPool = g_pMemPool;
*/
	{
		const char* pszServerName = g_pCoreConfigSection->GetString("Title");
		if(pszServerName)
			Log(LOG_SYSTEM, "Title: %s\n", pszServerName);

#ifdef _WIN32
		// if not service mode (headless)
		if(g_pszServiceName == NULL)
		{
			// set fancy console window title
			if(pszServerName != NULL)
			{
				SetConsoleTitleA(pszServerName);
			}
			else
			{
				char title[256];
				StringCchPrintfA(title, 256, "ServerCore - %s", g_pszConfigIniFile);
				SetConsoleTitleA(title);
			}

			// set console window position if specified
			const char* pszWinPos = g_pCoreConfigSection->GetString("ConsoleWindowPos");
			if(pszWinPos != NULL)
			{
				char szWinPos[64];
				StringCchCopyA(szWinPos, 64, pszWinPos);
				char* pComma = strchr(szWinPos, ',');
				if(pComma)
				{
					*pComma = 0;
					int xPos = atoi(szWinPos);
					int yPos = atoi(&pComma[1]);
					Log(LOG_SYSTEM, "Set Console Window position to (%d,%d)\n", xPos, yPos);

					HWND hConWin = GetConsoleWindow();
					SetWindowPos(hConWin, HWND_TOP, xPos, yPos, 0,0, SWP_NOSIZE);
				}
			}

			if (g_pCoreConfigSection->GetBoolean("HideConsoleWindow"))
			{
				HWND hConWin = GetConsoleWindow();
				ShowWindow(hConWin, SW_HIDE);
			}
		}
#endif
	}

#ifdef _WIN32
	srand(GetTickCount());
	if(!InitWinsock())
		goto _finish;
#endif

	// CORE Configuration
	m_nMaxSendBuffersPerConn = g_pCoreConfigSection->GetInteger("MaxSendBuffersPerConn", 2);

	g_nSendBufferSize = g_pCoreConfigSection->GetInteger("SendBufferSize", 4096);
	if (g_nSendBufferSize < 1024)
	{
		Log(LOG_FATAL, "Minimum SendBufferSize is 1024\n");
		goto _finish;
	}
	if (g_nSendBufferSize > (32 * 1024))
	{
		Log(LOG_FATAL, "Too big SendBufferSize: %d\n", g_nSendBufferSize);
		goto _finish;
	}


	if(!CreateIOCP())
	{
		goto _finish;
	}

	g_bServerStartedListening = g_pCoreConfigSection->GetBoolean("AutoStart", true);

	if(!ReadServerPartInfo(g_pCoreConfigSection))
		goto _finish;

	if(!LoadModules(g_pCoreConfigSection))
	{
		Log(LOG_FATAL, "Module loading failed.\n");
		goto _finish;
	}

	if(!OpenDatabase())
	{
		goto _finish;
	}

#ifdef SC_PLATFORM_POSIX
	if(!g_cioq.Create())
		goto _finish;
	if(!g_pollerWaker.Create())
		goto _finish;
#else
	CreateMainMonitorEvents();
#endif

	if(!InitializeModules())
	{
		Log(LOG_FATAL, "Module initialization failed.\n");
		goto _finish;
	}

	if(!g_clipool.Finalize())
		goto _finish;

	if(!SpawnJobThreads())
		goto _finish;

	if(m_bDbErrorOccur)
	{
		Log(LOG_FATAL, "Stopped by previous ODBC or thread exception.\n");
		goto _finish;
	}

	if(!BindDbStatementParams())
		goto _finish;

	g_bServerRunning = true;

	if(!g_sbpool.Create(g_pCoreConfigSection->GetInteger("InitialSendBuffers", 64)))
		goto _finish;

	if(g_bServerStartedListening)
	{
		g_bServerStartedListening = false;
		if(!StartListen())
			goto _finish;
	}

	// spawn network threads
	{
		int nNetworkThreads = g_pCoreConfigSection->GetInteger("NetworkThreads", 2);
		if(nNetworkThreads <= 0)
		{
			Log(LOG_FATAL, "Invalid NetworkThread number: %d\n", nNetworkThreads);
			goto _finish;
		}

		SC_ASSERT(m_nWorkThreadCount == 0);
		ZeroMemory(m_aWorkThreadHandles, sizeof(m_aWorkThreadHandles));
		ZeroMemory(m_aWorkThreadIDs, sizeof(m_aWorkThreadIDs));

		if(!CreateNetworkerThreads(nNetworkThreads))
			goto _finish;
	}

	EnterMainLoop();
	s_hasEverEnterMainLoop = true;

	KickAllSockets();
	TerminateNetworkThreads();

_finish:
	CloseAcceptSockets();
	CloseConnectSockets();
	TerminateJobThreads();
	g_clipool.Destroy();
	CloseIOCP();
//	g_sbpool.Destroy();

#ifdef SC_PLATFORM_POSIX
	g_cioq.Destroy();
	g_pollerWaker.Destroy();
#endif
	
	Hazard_OnFinalReclaimAll();
	FreeModules();
	DestroyJobQueues();
	DestroySockets();
	CloseDbStatements();
	CloseDatabase();

#ifdef _WIN32
	WSACleanup();
#endif

//	g_pMemPool->Destroy();
//	delete g_pMemPool;

	g_poolAllocs.FreeAll();
}

//////////////////////////////////////////////////////////////////////////////

void _ServerCoreMain()
{
	//g_pConfFile = new ConfigFile();
	SC_ASSERT(g_pConfFile);

	if(g_pConfFile->Load(g_pszConfigIniFile))
	{
		g_pCoreConfigSection = g_core.GetConfigSection(g_pszCoreConfigSectionName);
		if(g_pCoreConfigSection == NULL)
		{
			EventLog_Error(_T("Config file does not have [ServerCore] section.\n"));
		}
		else
		{
			if(LogOpen())
			{

#ifdef _WIN32
				__try { g_core.Main(); }
				__except (CreateCrashDump(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {}
#else
#endif

				Log(LOG_DATE|LOG_SYSTEM, "ServerCore exits.\n");
			}

			LogClose();
		}
	}

	delete g_pConfFile;
}

SC_NAMESPACE_END

using namespace svrcore;

int _tmain(int argc, const TCHAR* argv[])
//int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		printf("ServerCore Framework %d.%d,1\n", 0, 0);
#ifdef _WIN32
		printf("Usage: %s <ConfigFile> [/s:<ServiceName>]\n", argv[0]);
#else
		printf("Usage: %s <ConfigFile> [--daemon]\n", argv[0]);
#endif
		return -1;
	}

#ifdef _DEBUG
#ifdef _WIN32
//	_CrtSetBreakAlloc(200);
	{
		int	DbgFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
		DbgFlag |= _CRTDBG_LEAK_CHECK_DF;
		//	DbgFlag |= _CRTDBG_DELAY_FREE_MEM_DF;
		//	DbgFlag &= ~_CRTDBG_CHECK_ALWAYS_DF;
		_CrtSetDbgFlag(DbgFlag);
	}
#else	// _WIN32
#endif
#endif	// _DEBUG

	memset(&__svrsideProxy, 0, sizeof(__svrsideProxy));
	g_pszConfigIniFile = argv[1];

	g_pConfFile = new ConfigFile();  // workaround for VC++ error C2712 (SEH)

#ifndef SC_PLATFORM_POSIX
	if(argc > 2 && _tcsncmp(argv[2], _T("/s:"), 3) == 0)
	{
		// nt service mode
		g_pszServiceName = &argv[2][3];

		// chdir to near exe
		TCHAR buff[MAX_PATH];
		StringCchCopy(buff, MAX_PATH, argv[0]);
		TCHAR* pLastSlash = _tcsrchr(buff, '\\');
		if(pLastSlash != NULL)
		{
			*pLastSlash = NULL;
			SetCurrentDirectory(buff);
		}

		StartNtService();
	}
#else
	if(argc > 2 && _tcscmp(argv[2], _T("--daemon")) == 0)
	{
		// daemon mode
		printf("Daemon mode not implemented yet.\n");
	}
#endif
	else
	{
		// foreground console application mode
		g_pszServiceName = NULL;
		_ServerCoreMain();
	}

#if defined(_WIN32) && defined(_DEBUG)
	if (!s_hasEverEnterMainLoop && IsWindowVisible(GetConsoleWindow()))
	{
		puts("\nPress Enter key to finish...");
		getchar();
	}
#endif

	return 0;
}
