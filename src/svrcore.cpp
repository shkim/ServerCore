#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

template<typename K>
struct CloseObjT
{
	inline void operator()(K* p)
	{
		p->Close();
	}
};

template<typename K>
struct DestroyObjT
{
	inline void operator()(K* p)
	{
		p->Destroy();
		delete p;
	}
};


ServerCore::ServerCore()
{
#ifndef SC_PLATFORM_POSIX
	m_aMainMonitorEvents[0] = NULL;
	m_aMainMonitorEvents[1] = NULL;
#endif

//	m_nThreadsCurrent = 0;
//	m_nThreadsBusy = 0;
	m_nWorkThreadCount = 0;
	m_nJobThreadCount = 0;
	m_nJobQueueCount = 0;
	m_bDbErrorOccur = false;

	m_CoreLock.Create();
	m_csTimer.Create();
}

ServerCore::~ServerCore()
{
#ifndef SC_PLATFORM_POSIX
	if(m_aMainMonitorEvents[0])
		CloseHandle(m_aMainMonitorEvents[0]);
	if(m_aMainMonitorEvents[1])
		CloseHandle(m_aMainMonitorEvents[1]);
#endif

	m_CoreLock.Destroy();
	m_csTimer.Destroy();
}

IServerStateQuery* ServerCore::GetServerStateQuery(const char* pszName)
{
	if(pszName == NULL)
	{
		return static_cast<IServerStateQuery*>(m_SvrParts.at(0));
	}

	for(size_t s=0; s<m_SvrParts.size(); s++)
	{
		ServerPart* pPart = m_SvrParts.at(s);
		if(_stricmp(pPart->m_szName, pszName) == 0)
		{
			return static_cast<IServerStateQuery*>(pPart);
		}
	}

	Log(LOG_ERROR, "GetServerStateQuery(%s): Server not found\n", pszName);
	return NULL;
}

IGlobalStateQuery* ServerCore::GetGlobalStateQuery()
{
	return static_cast<IGlobalStateQuery*>(this);
}

//////////////////////////////////////////////////////////////////////////////

bool ServerCore::Connect(ITcpSocketListener* pListener, const char* pszAddress, unsigned short nRemotePort, unsigned short nLocalPort)
{
	TcpSocket* pSocket;

	m_CoreLock.Lock();
	if(m_FreeConnectSockets.empty())
	{
		pSocket = new TcpSocket();
	}
	else
	{
		pSocket = m_FreeConnectSockets.back();
		m_FreeConnectSockets.pop_back();
	}
	m_CoreLock.Unlock();

	int err = pSocket->Create();
	if(err != 0)
	{
		pListener->OnConnect(NULL, err);

_pushToFreeList:
		pSocket->Destroy();
		m_CoreLock.Lock();
		m_FreeConnectSockets.push_back(pSocket);
		m_CoreLock.Unlock();
		return false;
	}

	if(!pSocket->Connect(pListener, pszAddress, nRemotePort, nLocalPort))
	{
		goto _pushToFreeList;
	}

	m_CoreLock.Lock();
	m_BusyConnectSockets.push_back(pSocket);
	m_CoreLock.Unlock();

	return true;
}

void ServerCore::DiscardConnectSocket(TcpSocket* pSocket)
{
	pSocket->Destroy();

	m_CoreLock.Lock();
	{
#ifdef _DEBUG
		std::list<TcpSocket*>::iterator itr = find(m_BusyConnectSockets.begin(), m_BusyConnectSockets.end(), pSocket);
		if(itr == m_BusyConnectSockets.end())
		{
			Log(LOG_FATAL, "DiscardConnectSocket: socket(0x%X) not found in busy list.\n", pSocket);
		}
		else
		{
			m_BusyConnectSockets.erase(itr);
		}
#else
		m_BusyConnectSockets.remove(pSocket);
#endif
		m_FreeConnectSockets.push_back(pSocket);
	}
	m_CoreLock.Unlock();
}

#ifdef SC_PLATFORM_POSIX

TcpSocket* ServerCore::GetFreeAcceptSocket()
{
	TcpSocket* pSocket;

	m_CoreLock.Lock();
	{
		if(m_FreeAcceptSockets.empty())
		{
			pSocket = NULL;
		}
		else
		{
			pSocket = m_FreeAcceptSockets.back();
			m_FreeAcceptSockets.pop_back();
		}
	}
	m_CoreLock.Unlock();

	if(pSocket == NULL)
	{
		pSocket = new TcpSocket();
		pSocket->Create();
	}

	return pSocket;
}

void ServerCore::DiscardAcceptSocket(TcpSocket* pSocket)
{
	pSocket->Destroy();

	m_CoreLock.Lock();
	m_FreeAcceptSockets.push_back(pSocket);
	m_CoreLock.Unlock();
}

#else

void ServerPart::DiscardMalfunctionSocket(TcpSocket* pSocket)
{
	m_PartLock.Lock();
	{
		std::vector<TcpSocket*>::iterator itr = m_AcceptSockets.begin();
		for (; itr != m_AcceptSockets.end(); ++itr)
		{
			if ((*itr) == pSocket)
			{
				m_AcceptSockets.erase(itr);
				--m_nCurCreatedSockets;
				
				pSocket->Close();
				pSocket->Destroy();
				delete pSocket;

				Log(LOG_WARN, "[%s] Malfunction socket destroyed.\n", m_szName);
				return;
			}
		}
	}
	m_PartLock.Unlock();

	Log(LOG_DATE|LOG_FATAL, "[%s] DiscardMalfunctionSocket: socket not found.\n", m_szName);
}

#endif

void ServerPart::KickAcceptSockets()
{
	SC_ASSERT(!g_bServerRunning);

	// CHECK: is this code safe in threading environment?
	// (maybe no more socket will be added to this vector, because the server is shutting down.)

#ifndef SC_PLATFORM_POSIX
	Log(LOG_SYSTEM, "Kick %d of %d sockets in [%s]\n", m_nConcurrentConn, m_AcceptSockets.size(), m_szName);

	for(std::vector<TcpSocket*>::iterator itr = m_AcceptSockets.begin();
		itr != m_AcceptSockets.end(); ++itr)
	{
		(*itr)->Kick(true);
	}
#else
	Log(LOG_SYSTEM, "Kick busy %d sockets in [%s]\n", m_BusySockets.size(), m_szName);

	for(std::list<TcpSocket*>::iterator itr = m_BusySockets.begin();
		itr != m_BusySockets.end(); ++itr)
	{
		(*itr)->Kick(true);
	}
#endif
}

void ServerCore::KickAllSockets()
{
	// kick listened(?) sockets
	for(size_t s=0; s<m_SvrParts.size(); s++)
	{
		ServerPart* pPart = m_SvrParts.at(s);
		if(pPart->IsTCP())
			pPart->KickAcceptSockets();
	}

	// wait for all Listener objects decoupled from socket
	int nRetryLimit = 50;
	while(g_clipool.GetBusyCount() > 0)
	{
		Sleep(100);
		if(--nRetryLimit <= 0)
		{
			// FIXME: some program gets timeout, i dunno.
			Log(LOG_DATE|LOG_SYSTEM, _T("Timeout for waiting all socket listeners be released: %d remained busy.\n"), g_clipool.GetBusyCount());
			CloseAcceptSockets();
			break;
		}
	}

	// kick connected sockets
	if(!m_BusyConnectSockets.empty())
	{
		for(std::list<TcpSocket*>::iterator itr = m_BusyConnectSockets.begin();
			itr != m_BusyConnectSockets.end(); ++itr)
		{
			// FIXME
			TcpSocket* pSocket = *itr;
			if (pSocket != NULL)
				pSocket->Kick();
		}

		Sleep(300);	// wait a moment for connected sockets' io handling
	}
}

void ServerPart::CloseAcceptSockets()
{
	if(IsUDP())
	{
		m_pUdpSocket->Close();
	}
	
	if(!IsTCP())
		return;

/*
	if(m_hListenSocket != INVALID_SOCKET)
	{
		closesocket(m_hListenSocket);
		m_hListenSocket = INVALID_SOCKET;
	}
*/
#ifndef SC_PLATFORM_POSIX
	for_each(m_AcceptSockets.begin(), m_AcceptSockets.end(), CloseObjT<TcpSocket>());
#else
	for_each(m_BusySockets.begin(), m_BusySockets.end(), CloseObjT<TcpSocket>());
#endif
}

void ServerCore::CloseAcceptSockets()
{
	for(size_t s=0; s<m_SvrParts.size(); s++)
	{
		m_SvrParts.at(s)->CloseAcceptSockets();
	}
}

void ServerCore::CloseConnectSockets()
{
	for_each(m_BusyConnectSockets.begin(), m_BusyConnectSockets.end(), CloseObjT<TcpSocket>());
	for_each(m_FreeConnectSockets.begin(), m_FreeConnectSockets.end(), CloseObjT<TcpSocket>());
}

void ServerPart::Destroy()
{
	if(m_hListenSocket != INVALID_SOCKET)
	{
		closesocket(m_hListenSocket);
		m_hListenSocket = INVALID_SOCKET;
	}

	if(m_pUdpSocket != NULL)
	{
		delete m_pUdpSocket;
		m_pUdpSocket = NULL;
	}

#ifndef SC_PLATFORM_POSIX
	for_each(m_AcceptSockets.begin(), m_AcceptSockets.end(), DestroyObjT<TcpSocket>());
	m_AcceptSockets.clear();
	m_nCurCreatedSockets = 0;
#else
	for_each(m_BusySockets.begin(), m_BusySockets.end(), DestroyObjT<TcpSocket>());
	m_BusySockets.clear();
#endif

	m_PartLock.Destroy();
	Log(LOG_SYSTEM, "[%s] Total RX %lld, TX %lld\n", m_szName, m_nTotalRecvBytes, m_nTotalSentBytes);
}

void ServerCore::DestroySockets()
{
	for_each(m_SvrParts.begin(), m_SvrParts.end(), DestroyObjT<ServerPart>());
	m_SvrParts.clear();

	for_each(m_BusyConnectSockets.begin(), m_BusyConnectSockets.end(), DestroyObjT<TcpSocket>());
	m_BusyConnectSockets.clear();

	for_each(m_FreeConnectSockets.begin(), m_FreeConnectSockets.end(), DestroyObjT<TcpSocket>());
	m_FreeConnectSockets.clear();

#ifdef SC_PLATFORM_POSIX
	for_each(m_FreeAcceptSockets.begin(), m_FreeAcceptSockets.end(), DestroyObjT<TcpSocket>());
	m_FreeAcceptSockets.clear();
#else
	if(g_hUdpWorkThread != NULL)
	{
		CloseHandle(g_hUdpWorkThread);
		g_hUdpWorkThread = NULL;
	}
#endif
}

//////////////////////////////////////////////////////////////////////////////

void ServerCore::SetTimer(ITimerListener* pListener, int nTimerID, int nTimeoutMS, bool bRepeat)
{
	if(nTimeoutMS <= 0)
	{
		ClearTimer(pListener, nTimerID);
	}
	else
	{
		TTimerItem ti;

#ifdef SC_PLATFORM_POSIX
		ti.nDefaultTimeout = nTimeoutMS;	
		ti.nExpireTime.QuadPart = GetTickCount() + ti.nDefaultTimeout;
#else
		GetSystemTimeAsFileTime((FILETIME*)&ti.nExpireTime);
		ti.nDefaultTimeout = nTimeoutMS;
		ti.nDefaultTimeout *= 10000;	// millisecond
		ti.nExpireTime.QuadPart += ti.nDefaultTimeout;
#endif

		ti.pListener = pListener;
		ti.nTimerID = nTimerID;
		ti.bRepeat = bRepeat;

		m_csTimer.Lock();
		m_TimerReconfJobs.push_back(ti);
		m_csTimer.Unlock();

#ifndef SC_PLATFORM_POSIX
		SetEvent(m_aMainMonitorEvents[1]);
#else
		//g_utsock.SendEvent(0);
		g_pollerWaker.Wake();
#endif
	}
}

// This method returns after the clear complete. (not asynchronous)
void ServerCore::ClearTimer(ITimerListener* pListener, int nTimerID)
{
	m_csTimer.Lock();
	{
		for(std::list<TTimerItem>::iterator itr = m_listTimers.begin();
			itr != m_listTimers.end();)
		{
			TTimerItem& ti = *itr;
			
			if(ti.pListener == pListener)
			{
				if(nTimerID == 0 || ti.nTimerID == nTimerID)
				{
//					m_listTimers.erase(itr++);
					itr = m_listTimers.erase(itr);
					continue;
				}				
			}

			itr++;
		}
	}
	m_csTimer.Unlock();
}

// Inserts the values from priority_queue to the sorted list.
// Returns the Sleep time(ms) until the first item's wake.
DWORD ServerCore::_InsertTimerJobs(std::priority_queue<TTimerItem>& addjobs)
{
	if(addjobs.empty())
		goto _addDone;

	if(!m_listTimers.empty())
	{
		std::list<TTimerItem>::iterator itr = m_listTimers.end();
		
		do
		{
			std::list<TTimerItem>::iterator inspos = itr;
			TTimerItem& cur = *--itr;

_nextti:
			if(addjobs.empty())
				goto _addDone;

			const TTimerItem& ti = addjobs.top();
			if(cur < ti)
			{
				inspos = m_listTimers.insert(inspos, ti);
				addjobs.pop();
				goto _nextti;
			}
		} while(itr != m_listTimers.begin());
	}

	while(!addjobs.empty())
	{
		const TTimerItem& ti = addjobs.top();
		m_listTimers.push_front(ti);
		addjobs.pop();
	}

_addDone:

	if(m_listTimers.empty())
	{
#ifdef SC_PLATFORM_POSIX
		return -1;
#else
		return INFINITE;
#endif
	}
	else
	{
		LARGE_INTEGER now, dur;

#ifdef _DEBUG
		// assert sorted
		{
			std::list<TTimerItem>::iterator itr = m_listTimers.begin();
			now = itr->nExpireTime;
			while(++itr != m_listTimers.end())
			{
				SC_ASSERT(now.QuadPart <= itr->nExpireTime.QuadPart);
				now = itr->nExpireTime;
			}
		}
#endif

#ifdef SC_PLATFORM_POSIX
		now.QuadPart = GetTickCount();
#else
		GetSystemTimeAsFileTime((FILETIME*)&now);
#endif
		dur.QuadPart = m_listTimers.front().nExpireTime.QuadPart - now.QuadPart;
		if(dur.QuadPart < 0)
			return 0;

#ifdef SC_PLATFORM_POSIX
//		Log(LOG_DEBUG, "next duration: %d\n", dur.LowPart);
		return dur.LowPart;
#else

#if 0// _DEBUG
		__int64 _next = dur.LowPart / 10000;
		Log(LOG_DEBUG, "next duration: %d\n", _next);
		return _next;
#else
		return (dur.LowPart / 10000);
#endif

#endif	// SC_PLATFORM_POSIX
	}
}

DWORD ServerCore::ReconfigureTimers()
{
	DWORD dwNextWait;
	std::priority_queue<TTimerItem> addjobs;

	m_csTimer.Lock();
#if defined(SC_PLATFORM_POSIX) && defined(_WIN32)
	if(m_TimerReconfJobs.empty())
	{
		m_csTimer.Unlock();
		return INFINITE;
	}
#else
	SC_ASSERT(!m_TimerReconfJobs.empty());
#endif

	// delete TO-BE-UPDATED items
	for(std::list<TTimerItem>::iterator itr = m_listTimers.begin();
		itr != m_listTimers.end();)
	{
		TTimerItem& ti = *itr;
		std::list<TTimerItem>::iterator i1 = itr++;
		
		for(std::vector<TTimerItem>::iterator i2 = m_TimerReconfJobs.begin();
			i2 != m_TimerReconfJobs.end(); i2++)
		{
			if(ti.pListener == i2->pListener 
			&& ti.nTimerID == i2->nTimerID)
			{
				m_listTimers.erase(i1);
				break;
			}
		}
	}

	for(std::vector<TTimerItem>::iterator i2 = m_TimerReconfJobs.begin();
		i2 != m_TimerReconfJobs.end(); i2++)
	{
		addjobs.push(*i2);
	}

	m_TimerReconfJobs.clear();

	dwNextWait = _InsertTimerJobs(addjobs);
	m_csTimer.Unlock();

	return dwNextWait;
}

SC_NAMESPACE_END