#include "stdafx.h"
#include "scimpl.h"
#include "evtlogmsg.h"

SC_NAMESPACE_BEGIN

DWORD WINAPI HandlerEx(DWORD dwControl, DWORD dwEventType, PVOID pvEventData, PVOID pvContext);

class EventLogger
{
public:
	EventLogger()
	{
		m_hEventLog = NULL;
	}

	~EventLogger()
	{
		if(m_hEventLog != NULL)
		{
			DeregisterEventSource(m_hEventLog);
		}
	}

	inline bool IsEnabled() const { return (m_hEventLog != NULL); }
	
	bool Create(LPCTSTR pszServiceName)
	{
		m_hEventLog = RegisterEventSource(NULL, pszServiceName);
		return (m_hEventLog != NULL);
	}

	void Report(DWORD nMsgID, WORD wNumStrings=0, LPCTSTR* pStrings=NULL)
	{
		WORD wType;

		switch(nMsgID >> 30)
		{
		case 0:
			wType = EVENTLOG_SUCCESS;
			break;
		case 1:
			wType = EVENTLOG_INFORMATION_TYPE;
			break;
		case 2:
			wType = EVENTLOG_WARNING_TYPE;
			break;
		case 3:
		default:
			wType = EVENTLOG_ERROR_TYPE;
			break;
		}

		WORD wCategory = 0;
		ReportEvent(m_hEventLog, wType, wCategory, nMsgID, NULL,
			wNumStrings, 0, pStrings, NULL);
	}

	void ReportNum(DWORD nMsgID, int n)
	{
		TCHAR str[64];
		LPCTSTR arr[1];

		StringCchPrintf(str, 64, _T("%d"), n);
		arr[0] = str;
		Report(nMsgID, 1, arr);
	}

	void ReportStr(DWORD nMsgID, const TCHAR* msg)
	{
		LPCTSTR arr[1];
		arr[0] = msg;
		Report(nMsgID, 1, arr);
	}

/*
	void ReportA(DWORD nMsgID, WORD wNumStrings=0, LPCSTR* pStrings=NULL)
	{
		WORD wType;

		switch(nMsgID >> 30)
		{
		case 0:
			wType = EVENTLOG_SUCCESS;
			break;
		case 1:
			wType = EVENTLOG_INFORMATION_TYPE;
			break;
		case 2:
			wType = EVENTLOG_WARNING_TYPE;
			break;
		case 3:
		default:
			wType = EVENTLOG_ERROR_TYPE;
			break;
		}

		WORD wCategory = 0;
		ReportEventA(m_hEventLog, wType, wCategory, nMsgID, NULL,
			wNumStrings, 0, pStrings, NULL);
	}

	void ReportW(DWORD nMsgID, WORD wNumStrings=0, LPCWSTR* pStrings=NULL)
	{
		WORD wType;

		switch(nMsgID >> 30)
		{
		case 0:
			wType = EVENTLOG_SUCCESS;
			break;
		case 1:
			wType = EVENTLOG_INFORMATION_TYPE;
			break;
		case 2:
			wType = EVENTLOG_WARNING_TYPE;
			break;
		case 3:
		default:
			wType = EVENTLOG_ERROR_TYPE;
			break;
		}

		WORD wCategory = 0;
		ReportEventW(m_hEventLog, wType, wCategory, nMsgID, NULL,
			wNumStrings, 0, pStrings, NULL);
	}

	void ReportStr(DWORD nMsgID, const char* msg)
	{
		LPCSTR arr[1];
		arr[0] = msg;
		ReportA(nMsgID, 1, arr);
	}

	void ReportStr(DWORD nMsgID, const WCHAR* msg)
	{
		LPCWSTR arr[1];
		arr[0] = msg;
		ReportW(nMsgID, 1, arr);
	}
*/
private:
	HANDLE m_hEventLog;
};

static EventLogger s_evtlog;

class ServiceStatus
{
public:
	ServiceStatus()
	{
		m_hSS = NULL;
		ZeroMemory(&m_ss, sizeof(m_ss));
	}

	bool RegisterHandler(LPCTSTR pszServiceName)
	{
		m_hSS = RegisterServiceCtrlHandlerEx(pszServiceName, HandlerEx, NULL);

		if(m_hSS == NULL)
		{
			DWORD dwLastError = GetLastError();
			switch(dwLastError)
			{
			case ERROR_SERVICE_DOES_NOT_EXIST:
				s_evtlog.Report(MSG_SERVICE_NOTEXIST);
				break;

			case ERROR_INVALID_NAME:
				s_evtlog.Report(MSG_SERVICE_INVALIDNAME);
				break;

			default:
				s_evtlog.ReportNum(MSG_SERVICE_REGISTERHANDLER_FAILED, dwLastError);
			}

			return false;			
		}

		m_ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
		m_ss.dwCurrentState = SERVICE_START_PENDING; 
		m_ss.dwControlsAccepted = 0;
		m_ss.dwWin32ExitCode = NO_ERROR;
		m_ss.dwCheckPoint = 1;
		m_ss.dwWaitHint = 3000;

		SetServiceStatus(m_hSS, &m_ss);
		return true;
	}

	void SetStateRunning()
	{
		m_ss.dwCurrentState = SERVICE_RUNNING;
		m_ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
		m_ss.dwCheckPoint = 0;
		m_ss.dwWaitHint = 0;

		SetServiceStatus(m_hSS, &m_ss);
	}

	void SetStateStopped()
	{
		m_ss.dwCurrentState = SERVICE_STOPPED;
		m_ss.dwControlsAccepted = 0;
		m_ss.dwCheckPoint = 0;
		m_ss.dwWaitHint = 0;

		SetServiceStatus(m_hSS, &m_ss);
	}

	void SetState(int status, int chkpnt, int waithint)
	{
		m_ss.dwCurrentState = status;
		m_ss.dwCheckPoint = chkpnt;
		m_ss.dwWaitHint = waithint;

		SetServiceStatus(m_hSS, &m_ss);
	}

	void OnInterrogate()
	{
		if(m_ss.dwCurrentState == SERVICE_RUNNING
		|| m_ss.dwCurrentState == SERVICE_STOPPED)
			m_ss.dwCheckPoint = 0;
		else
			m_ss.dwCheckPoint++;
		
		SetServiceStatus(m_hSS, &m_ss);
	}

private:
	SERVICE_STATUS_HANDLE m_hSS;
	SERVICE_STATUS m_ss;
};

static ServiceStatus s_ss;

DWORD WINAPI HandlerEx(DWORD dwControl, DWORD dwEventType, PVOID pvEventData, PVOID pvContext)
{
	UNREFERENCED_PARAMETER(dwEventType);
	UNREFERENCED_PARAMETER(pvEventData);
	UNREFERENCED_PARAMETER(pvContext);

	DWORD dwReturn = ERROR_CALL_NOT_IMPLEMENTED;
	bool bFireShutdown = false;

	switch (dwControl) 
	{
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		s_ss.SetState(SERVICE_STOP_PENDING, 1, 3000);
		bFireShutdown = true;
		break;

	case SERVICE_CONTROL_INTERROGATE:
		s_ss.OnInterrogate();
		break;
	}

	if(bFireShutdown) 
	{
		Log(LOG_DATE|LOG_SYSTEM, _T("Got service event: %d\n"), dwControl);
		g_core.Shutdown();
		dwReturn = NO_ERROR;
	}

	return dwReturn;
}

void EventLog_Error(const TCHAR* format, ...)
{
	TCHAR szBuffer[1024];
	va_list ap;

	va_start(ap, format);
	StringCchVPrintf(szBuffer, 1024, format, ap);	
	va_end(ap);

	if(s_evtlog.IsEnabled())
		s_evtlog.ReportStr(MSG_REPORT_ERROR, szBuffer);
	else
		_tprintf(_T("%s"), szBuffer);
}

void EventLog_LogReport(LPCTSTR msg)
{
	SC_ASSERT(s_evtlog.IsEnabled());
	s_evtlog.ReportStr(MSG_REPORT_START, msg);
}

void ReportServiceStarted()
{
	s_ss.SetStateRunning();
}

void _ServerCoreMain();

static void WINAPI ServiceMain(DWORD dwArgc, PTSTR* pszArgv)
{
	UNREFERENCED_PARAMETER(dwArgc);
	UNREFERENCED_PARAMETER(pszArgv);

	if(s_ss.RegisterHandler(g_pszServiceName))
	{
		_ServerCoreMain();

		s_evtlog.Report(MSG_REPORT_FINISH);
		s_ss.SetStateStopped();
	}
}

void StartNtService()
{
	SERVICE_TABLE_ENTRY ServiceTable[] = {
		{ _T(""), ServiceMain },	// "" is okay since SERVICE_WIN32_OWN_PROCESS.
		{ NULL, NULL }   // End of list
	};

	if(!s_evtlog.Create(g_pszServiceName))
		return;

	StartServiceCtrlDispatcher(ServiceTable);
}

SC_NAMESPACE_END
