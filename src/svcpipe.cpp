#include "stdafx.h"
#include "scimpl.h"

#if 0

SC_NAMESPACE_BEGIN

// NT Service will create a process for ServerCore instance,
// and uses the Named Pipe to communicate between them.
// IocpPipe class is or that purpose. Only one object is instanced (singleton)

IocpPipe::IocpPipe()
{
	m_hPipe = INVALID_HANDLE_VALUE;

	ZeroMemory(&m_ovRecv, sizeof(m_ovRecv));
	ZeroMemory(&m_ovSend, sizeof(m_ovSend));
	m_ovRecv.pPipeObject = this;
	m_ovSend.pPipeObject = this;
}

IocpPipe::~IocpPipe()
{
	if(m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}
}

bool IocpPipe::Connect(LPCTSTR pszServiceName)
{
	TCHAR szPipeName[MAX_PATH];

	ASSERT(m_hPipe == INVALID_HANDLE_VALUE);

	StringCchPrintf(szPipeName, MAX_PATH, _T("\\\\.\\pipe\\svrcore\\%s"), pszServiceName);
	m_hPipe = CreateFile(szPipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	
	if(m_hPipe == INVALID_HANDLE_VALUE)
	{
		Log(LOG_DATE|LOG_SYSTEM, "Creating NamedPipe to service(%s) failed.\n", pszServiceName);
		return false;
	}

	m_pszServiceName = pszServiceName;

	m_bDisconnected = false;
	m_nSendBuffLen = 0;
//	m_nRecvBuffLen = 0;

	if(!AssociateDeviceWithCompletionPort(g_hCompPort, m_hPipe, (ULONG_PTR) this))
	{
		Log(LOG_SYSTEM, "IocpPipe::Create - AssociateDeviceWithCompletionPort failed: err=%d\n", GetLastError());
		return false;
	}

	// initial recv post
	PostRecv();

	return true;
}

void IocpPipe::Disconnect()
{
	if(m_hPipe == INVALID_HANDLE_VALUE)
	{
		Log(LOG_SYSTEM, "SvcPipe::Disconnect: hPipe is invalid\n");
		return;
	}

	if(m_bDisconnected)
	{
		Log(LOG_SYSTEM, "Already disconnected - SvcPipe::Disconnect() call ignored.\n");
		return;
	}

	Log(LOG_REPORT, "Disconnecting service pipe.\n");

	m_bDisconnected = true;

	CloseHandle(m_hPipe);
	m_hPipe = INVALID_HANDLE_VALUE;

	g_core.ShutdownCore();
}

void IocpPipe::OnRecvComplete(char* pBuffer, DWORD cbSize)
{
	pBuffer[cbSize] = 0;

	if(strcmp(pBuffer, "SHUTDOWN") == 0)
	{
		Log(LOG_DATE|LOG_REPORT, "Got SHUTDOWN command from Service Pipe.\n");
		Disconnect();
		return;
	}
	else if(strncmp(pBuffer, "HELLO", 5) == 0)
	{
		// check service name matches
		if(strcmp(&pBuffer[6], m_pszServiceName) == 0)
		{
			LogReportToService(this);
		}
		else
		{
			Log(LOG_SYSTEM, "Got invalid service name from pipe: %s\n", &pBuffer[6]);
			Disconnect();
			return;
		}
	}
	else
	{
		Log(LOG_SYSTEM, "Got invalid command from service pipe: %s\n", pBuffer);
		Disconnect();
		return;
	}

	PostRecv();
}

void IocpPipe::OnSendComplete(char* pBuffer, DWORD cbSize)
{
}

void IocpPipe::Send(void* p, int len)
{
	DWORD dwWritten;

	if(m_bDisconnected)
	{
		Log(LOG_SYSTEM, "Pipe:Send() ignored (disconnected)\n", len);
	}
	else if(len > MAX_PIPE_SENDBUFFER_SIZE)
	{
		Log(LOG_SYSTEM, "Pipe:Send buffer too big: %d\n", len);
	}
	else if(m_nSendBuffLen > 0)
	{
		Log(LOG_SYSTEM, "Pipe:Send() call ignored (previous send not completed)\n", len);
	}
	else
	{
		memcpy(m_sendbuff, p, len);
		m_nSendBuffLen = len;

		if(!WriteFile(m_hPipe, m_sendbuff, len, &dwWritten, &m_ovSend))
		{
			int nLastError = GetLastError();
			if(nLastError != ERROR_IO_PENDING)
			{
				Log(LOG_SYSTEM, "Pipe:Send WriteFile failed: err=%d\n", nLastError );
				Disconnect();
			}
		}
	}
}

void IocpPipe::PostRecv()
{
	DWORD dwRead;

	if(m_bDisconnected)
	{
		Log(LOG_SYSTEM, "Pipe::PostRecv() canceled by disconn.\n");
		return;
	}

	ZeroMemory(&m_ovRecv, sizeof(WSAOVERLAPPED));
	ASSERT(m_ovRecv.pPipeObject == this);
	m_ovRecv.type = OVTYPE_PIPE_READING;
	m_ovRecv.pRecvBuffer = m_recvbuff;

	if(!ReadFile(m_hPipe, m_recvbuff, sizeof(m_recvbuff), &dwRead, &m_ovRecv))
	{
		int nLastError = GetLastError();

		if(nLastError != ERROR_IO_PENDING)
		{
			Log(LOG_SYSTEM, "Pipe::ReadFile fail: %d\n", nLastError);
			Disconnect();
		}
	}
}

bool ServerCore::ConnectServicePipe(LPCTSTR pszServiceName)
{
	if(m_pServicePipe == NULL)
	{
		m_pServicePipe = new IocpPipe();
		return m_pServicePipe->Connect(pszServiceName);
	}

	return false;
}

SC_NAMESPACE_END

#endif