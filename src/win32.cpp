#include "stdafx.h"
#include "scimpl.h"
#include "../extern/dbghelp/inc/dbghelp.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment (lib, "dbghelp.lib")

SC_NAMESPACE_BEGIN

LPFN_ACCEPTEX g_lpfnAcceptEx = NULL;
LPFN_GETACCEPTEXSOCKADDRS g_lpfnGetAcceptExSockaddrs = NULL;
LPFN_TRANSMITFILE g_lpfnTransmitFile = NULL;
LPFN_CONNECTEX g_lpfnConnectEx = NULL;
LPFN_DISCONNECTEX g_lpfnDisconnectEx = NULL;
HANDLE g_hUdpWorkThread = NULL;

bool InitWinsock()
{
	WSADATA wsaData;

	if(0 != WSAStartup(MAKEWORD(2,2), &wsaData))
	{
		Log(LOG_FATAL, "WSAStartup failed; err=%d\n", WSAGetLastError());
		return false;
	}

	// confirm winsock dll supports 2.2
	if( LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2 )
	{
		WSACleanup();
		Log(LOG_FATAL, "Winsock version: %d.%d (< 2.2)\n", 
			LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
		return false;
	}

#ifndef SC_PLATFORM_POSIX
	if(!GetExtSockFuncs())
		return false;
#endif

	return true;
}

bool GetExtSockFuncs()
{
	DWORD dwBytes;
	bool bSuccess = false;

	GUID GuidConnectEx = WSAID_CONNECTEX;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
	GUID GuidGetAcceptSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	GUID GuidTransmitFile = WSAID_TRANSMITFILE;


	SOCKET hTempSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 
		NULL, 0, WSA_FLAG_OVERLAPPED);

	if(hTempSocket == INVALID_SOCKET)
	{
		Log(LOG_FATAL, "GetExtSockFuncs: TempSocket creation failed.\n");
		return false;
	}


	if(SOCKET_ERROR == WSAIoctl(hTempSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx, sizeof(GuidAcceptEx),
		&g_lpfnAcceptEx, sizeof(g_lpfnAcceptEx),
		&dwBytes, NULL, NULL))
	{
		Log(LOG_FATAL, "WSAIoctl(AcceptEx) failed.\n");
		goto _extfail;
	}

	if(SOCKET_ERROR == WSAIoctl(hTempSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptSockAddrs, sizeof(GuidGetAcceptSockAddrs),
		&g_lpfnGetAcceptExSockaddrs, sizeof(g_lpfnGetAcceptExSockaddrs),
		&dwBytes, NULL, NULL))
	{
		Log(LOG_FATAL, "WSAIoctl(GetAcceptExSockaddrs) failed.\n");
		goto _extfail;
	}

	if(SOCKET_ERROR == WSAIoctl(hTempSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidTransmitFile, sizeof(GuidTransmitFile),
		&g_lpfnTransmitFile, sizeof(g_lpfnTransmitFile),
		&dwBytes, NULL, NULL))
	{
		Log(LOG_FATAL, "WSAIoctl(TransmitFile) failed.\n");
		goto _extfail;
	}

	// Windows 2000 does NOT SUPPORT DisconnectEx

	if(SOCKET_ERROR == WSAIoctl(hTempSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidConnectEx, sizeof(GuidConnectEx),
		&g_lpfnConnectEx, sizeof(g_lpfnConnectEx),
		&dwBytes, NULL, NULL))
	{
		Log(LOG_FATAL, "WSAIoctl(ConnectEx) failed.\n");
		goto _extfail;
	}

	if(SOCKET_ERROR == WSAIoctl(hTempSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidDisconnectEx, sizeof(GuidDisconnectEx),
		&g_lpfnDisconnectEx, sizeof(g_lpfnDisconnectEx),
		&dwBytes, NULL, NULL))
	{
		Log(LOG_FATAL, "WSAIoctl(DisconnectEx) failed.\n");
		goto _extfail;
	}

	bSuccess = true;

_extfail:
	closesocket(hTempSocket);
	return bSuccess;
}

const char* GetWsaErrorString(int nErrorCode)
{
	static TCHAR unknown_error[32];

	switch(nErrorCode)
	{
	case WSANOTINITIALISED:
		return "WSANOTINITIALISED";
	case WSAENETDOWN:
		return "WSAENETDOWN";
	case WSAEACCES:
		return "WSAEACCES";
	case WSAEINTR:
		return "WSAEINTR";
	case WSAEINPROGRESS:
		return "WSAEINPROGRESS";
	case WSAEFAULT:
		return "WSAEFAULT";
	case WSAENETRESET:
		return "WSAENETRESET";
	case WSAENOBUFS:
		return "WSAENOBUFS";
	case WSAENOTCONN:
		return "WSAENOTCONN";
	case WSAENOTSOCK:
		return "WSAENOTSOCK";
	case WSAEOPNOTSUPP:
		return "WSAEOPNOTSUPP";
	case WSAESHUTDOWN:
		return "WSAESHUTDOWN";
	case WSAEWOULDBLOCK:
		return "WSAEWOULDBLOCK";
	case WSAEMSGSIZE:
		return "WSAEMSGSIZE";
	case WSAEINVAL:
		return "WSAEINVAL";
	case WSAECONNABORTED:
		return "WSAECONNABORTED";
	case WSAECONNRESET:
		return "WSAECONNRESET";
	case WSAECONNREFUSED:
		return "WSAECONNREFUSED";
	case WSA_IO_PENDING:
		return "WSA_IO_PENDING";
	case WSA_OPERATION_ABORTED:
		return "WSA_OPERATION_ABORTED";

	default:
		StringCchPrintfA(unknown_error, 32, "WSAE(%d)", nErrorCode);
		return unknown_error;
	}
}

//////////////////////////////////////////////////////////////////////////////

#if _WIN32_WINNT <= 0x0501

__int64 _InterlockedExchangeAdd64(__int64 volatile* addend, __int64 value)
{
	__int64 before, result;

	do {
		before = *addend;
		result = before + value;
	} while(_InterlockedCompareExchange64(addend, result, before) != before);

	return before;
}

#endif

BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
{
	Log(LOG_DATE|LOG_SYSTEM, "Got console event: %d\n", dwCtrlType);
	g_core.Shutdown();
	return TRUE;
}

#ifndef SC_PLATFORM_POSIX

HANDLE CreateNewCompletionPort(DWORD dwNumberOfConcurrentThreads)
{
	return CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, dwNumberOfConcurrentThreads);
}

BOOL AssociateDeviceWithCompletionPort(HANDLE hCompPort, HANDLE hDevice, ULONG_PTR nCompKey)
{
	HANDLE h = CreateIoCompletionPort(hDevice, hCompPort, nCompKey, 0);
	return (h == hCompPort);
}

int ServerCore::GetMemoryLoad()
{
	return 0;
}

int ServerCore::GetCpuLoad(char aUsage[])
{
	UNREFERENCED_PARAMETER(aUsage); // TODO
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

void ServerCore::OnServerShutdown()
{
	SetEvent(m_aMainMonitorEvents[0]);
}

void ServerCore::CreateMainMonitorEvents()
{
	TCHAR szName[64];
	DWORD dwProcessID = GetCurrentProcessId();
	StringCchPrintf(szName, 64, _T("Global\\KillSvrCore%d"), dwProcessID);

	// manual-reset event
	m_aMainMonitorEvents[0] = CreateEvent(NULL, TRUE, FALSE, szName);
	m_aMainMonitorEvents[1] = CreateEvent(NULL, TRUE, FALSE, NULL);

	Log(LOG_SYSTEM, "Shutdown Event name: %s\n", szName);
}

bool ServerCore::CreateIOCP()
{	
	SC_ASSERT(g_hCompPort == INVALID_HANDLE_VALUE);

	// Create main (unique) IOCP object
	g_hCompPort = CreateNewCompletionPort(0);
	if(g_hCompPort == INVALID_HANDLE_VALUE)
	{
		Log(LOG_FATAL, "IOCP creation failed\n");
		return false;
	}

	return true;
}

void ServerCore::CloseIOCP()
{
	// close iocp handle
	if(g_hCompPort != INVALID_HANDLE_VALUE)
	{
		CloseHandle(g_hCompPort);		
		g_hCompPort = INVALID_HANDLE_VALUE;
	}
}

bool ServerCore::CreateNetworkerThreads(int nThreadCnt)
{
	for(int i=0; i<nThreadCnt; i++)
	{
		m_aWorkThreadHandles[i] = (HANDLE) _beginthreadex(NULL, 0, TcpSocket::WorkerThreadProc, NULL, 0, &m_aWorkThreadIDs[i]);

		if(m_aWorkThreadHandles[i] == INVALID_THREAD_HANDLE_VALUE)
		{
			Log(LOG_FATAL, "IocpWorker: beginthreadex() failed\n");
			return false;
		}

		m_nWorkThreadCount++;
	}	

	SC_ASSERT(g_hUdpWorkThread == NULL);
	if(!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
		GetCurrentProcess(), &g_hUdpWorkThread,
		0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		Log(LOG_FATAL, "DuplicateHandle(main thread) failed\n");
		return false;
	}

	return true;
}

void ServerCore::TerminateNetworkThreads()
{
	// Finish all IOCP threads
	Log(LOG_SYSTEM, _T("Terminate %d network thread(s)\n"), m_nWorkThreadCount);

	for(int i=0; i<m_nWorkThreadCount; i++)
	{
		PostQueuedCompletionStatus(g_hCompPort, 0, CK_TERMINATE_THREAD, NULL);
	}

	int ret = WaitForMultipleObjects(m_nWorkThreadCount, m_aWorkThreadHandles, TRUE, 10*1000);
	if(ret == WAIT_TIMEOUT)
	{
		Log(LOG_SYSTEM, _T("Main thread failed to wait for all network threads' end. (timeout)\n") );
	}

	// Terminate any IOCP thread which is not cleanly finished.
	for(int i=0; i<m_nWorkThreadCount; i++)
	{
		if(m_aWorkThreadHandles[i] != INVALID_THREAD_HANDLE_VALUE)
		{
			CloseHandle(m_aWorkThreadHandles[i]);
			m_aWorkThreadHandles[i] = INVALID_THREAD_HANDLE_VALUE;
		}
	}
}

void ServerCore::EnterMainLoop()
{
	if(g_pszServiceName == NULL)
	{
		SetConsoleCtrlHandler(CtrlHandler, TRUE);
	}
	else
	{
		ReportServiceStarted();
	}

	DWORD ret;
	DWORD dwNextWait = INFINITE;

_rewait:
	ret = WaitForMultipleObjectsEx(2, m_aMainMonitorEvents, FALSE, dwNextWait, TRUE);

	if(ret == WAIT_OBJECT_0)
	{
		// shutdown event
		g_bServerRunning = false;
	}
	else
	{
		if(ret == WAIT_IO_COMPLETION)
		{
			dwNextWait = INFINITE;//RecalcNextTimeout();
		}
		else if(ret == WAIT_TIMEOUT)
		{
			dwNextWait = OnTimeout();
		}
		else if(ret == (WAIT_OBJECT_0+1))
		{
			dwNextWait = ReconfigureTimers();
			ResetEvent(m_aMainMonitorEvents[1]);
		}
		else
		{
			Log(LOG_FATAL, "Unknown ret(%d) of main WaitForMultipleObjects\n", ret);
			std::priority_queue<TTimerItem> _dummy;
			dwNextWait = _InsertTimerJobs(_dummy);
		}

		goto _rewait;
	}

	Log(LOG_SYSTEM, "Graceful server shutdown.\n");
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

DWORD ServerCore::OnTimeout()
{
	LARGE_INTEGER now;
	DWORD dwNextWait;
	std::priority_queue<TTimerItem> addjobs;
	std::vector<TTimerItem> timeouts;

	GetSystemTimeAsFileTime((FILETIME*)&now);
	now.QuadPart += (USER_TIMER_MINIMUM * 10000);

	m_csTimer.Lock();
	{
		for(std::list<TTimerItem>::iterator itr = m_listTimers.begin();
			itr != m_listTimers.end();)
		{
			TTimerItem& ti = *itr;

			if(ti.nExpireTime.QuadPart <= now.QuadPart)
			{
				timeouts.push_back(ti);

				if(ti.bRepeat)
				{
					ti.nExpireTime.QuadPart += ti.nDefaultTimeout;
					addjobs.push(ti);
				}

				m_listTimers.erase(itr++);
				continue;
			}

			break;
		}

		dwNextWait = _InsertTimerJobs(addjobs);
	}
	m_csTimer.Unlock();

	for(std::vector<TTimerItem>::iterator itr = timeouts.begin(); itr != timeouts.end(); itr++)
	{
		ITimerListener* pLsnr = itr->pListener;
		if(pLsnr->Ref_Retain() > 1)
			pLsnr->OnTimer(itr->nTimerID);
		pLsnr->Ref_Release();
	}

	return dwNextWait;
}

///////////////////////////////////////////////////////////////////////////////
// from www.debuginfo.com

// This function determines whether we need data sections of the given module 
static bool IsDataSectionNeeded(const WCHAR* pModuleName)
{
	if (pModuleName == 0)
	{
		SC_ASSERT(!"Parameter is null.");
		return false;
	}

	// Extract the module name 
	WCHAR szFileName[_MAX_FNAME] = L"";
	_wsplitpath_s(pModuleName, NULL, 0, NULL, 0, szFileName, _MAX_FNAME, NULL, 0);

	// Compare the name with the list of known names and decide 
	// Note: For this to work, the executable name must be "ServerCore.exe"
	if (_wcsicmp(szFileName, L"ServerCore") == 0)
	{
		return true;
	}
	else if (_wcsicmp(szFileName, L"ntdll") == 0)
	{
		return true;
	}

	// Complete 
	return false;
}

static BOOL CALLBACK MyMiniDumpCallback(PVOID pParam, const PMINIDUMP_CALLBACK_INPUT pInput, PMINIDUMP_CALLBACK_OUTPUT pOutput)
{
	BOOL bRet = FALSE;

	if (pInput == 0)
		return FALSE;

	if (pOutput == 0)
		return FALSE;

	// Process the callbacks 
	switch (pInput->CallbackType)
	{
	case IncludeModuleCallback:
		// Include the module into the dump 
		bRet = TRUE;
		break;

	case IncludeThreadCallback:
		// Include the thread into the dump 
		bRet = TRUE;
		break;

	case ModuleCallback:
		// Are data sections available for this module ? 
		if (pOutput->ModuleWriteFlags & ModuleWriteDataSeg)
		{
			// Yes, they are, but do we need them? 
			if (!IsDataSectionNeeded(pInput->Module.FullPath))
			{
				wprintf(L"Excluding module data sections: %s \n", pInput->Module.FullPath);
				pOutput->ModuleWriteFlags &= (~ModuleWriteDataSeg);
			}
		}
		bRet = TRUE;
		break;

	case ThreadCallback:
		// Include all thread information into the minidump 
		bRet = TRUE;
		break;

	case ThreadExCallback:
		// Include this information 
		bRet = TRUE;
		break;

	case MemoryCallback:
		// We do not include any information here -> return FALSE 
		bRet = FALSE;
		break;

	case CancelCallback:
		break;
	}

	return bRet;
}

void CreateCrashDump(EXCEPTION_POINTERS* pep)
{
	if (IsDebuggerPresent())
		return;

	// TODO: create in log directory

	HANDLE hFile = CreateFile(_T("ServerCore.dmp"), GENERIC_READ | GENERIC_WRITE,
		0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if ((hFile != NULL) && (hFile != INVALID_HANDLE_VALUE))
	{
		MINIDUMP_EXCEPTION_INFORMATION mdei;

		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pep;
		mdei.ClientPointers = FALSE;

		MINIDUMP_CALLBACK_INFORMATION mci;

		mci.CallbackRoutine = (MINIDUMP_CALLBACK_ROUTINE)MyMiniDumpCallback;
		mci.CallbackParam = 0;

		MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithPrivateReadWriteMemory |
			MiniDumpWithDataSegs |
			MiniDumpWithHandleData |
			MiniDumpWithFullMemoryInfo |
			MiniDumpWithThreadInfo |
			MiniDumpWithUnloadedModules);

		BOOL rv = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
			hFile, mdt, (pep != 0) ? &mdei : 0, 0, &mci);

		if (!rv)
			_tprintf(_T("MiniDumpWriteDump failed. Error: %u \n"), GetLastError());
		else
			_tprintf(_T("Minidump created.\n"));

		// Close the file 
		CloseHandle(hFile);

	}
	else
	{
		_tprintf(_T("CreateFile failed. Error: %u \n"), GetLastError());
	}
}

#endif	// SC_PLATFORM_POSIX

SC_NAMESPACE_END
