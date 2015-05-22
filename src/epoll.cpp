#include "stdafx.h"
#include "scimpl.h"

#ifdef __linux__

#define SC_EPOLL_EVENTMAX		256

bool ServerCore::CreateIOCP()
{
	ASSERT(g_hCompPort == INVALID_HANDLE_VALUE);

	g_hCompPort = epoll_create(SC_EPOLL_EVENTMAX);
	if(g_hCompPort < 0)
	{
		Log(LOG_FATAL, "epoll: create failed (errno=%d)\n", errno);
		return false;
	}

	return true;
}

void ServerCore::CloseIOCP()
{
	if(g_hCompPort > 0)
	{
		close((int)g_hCompPort);
		g_hCompPort = 0;
	}
}

bool AssociateWithPoller(SOCKET hSocket, IoEventListener* pListener)
{
	struct epoll_event evt;

	evt.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
	evt.data.ptr = pListener;

	if(epoll_ctl(g_hCompPort, EPOLL_CTL_ADD, hSocket, &evt) == -1)
	{
		Log(LOG_FATAL, "epoll_ctl: ADD failed (errno=%d)\n", errno);
		return false;
	}

//	Log(LOG_INFO, "epoll_ctl: associate %x,%p\n", hSocket, pObject);
	return true;
}


void SetSendableInterestWithPoller(SOCKET hSocket, IoEventListener* pListener, bool bNotifySendable)
{
	struct epoll_event evt;

	evt.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET | (bNotifySendable ? EPOLLOUT : 0);
	evt.data.ptr = pListener;

	if(epoll_ctl(g_hCompPort, EPOLL_CTL_MOD, hSocket, &evt) == -1)
	{
		Log(LOG_FATAL, "epoll_ctl: MODIFY failed (errno=%d)\n", errno);
	}
}

//////////////////////////////////////////////////////////////////////////////

void CompletedIoQueue::EnterPoller()
{
	struct epoll_event evts[SC_EPOLL_EVENTMAX];
	int num_fds;
	DWORD timeout = -1;

	ASSERT(g_bServerRunning);
	while(g_bServerRunning)
	{
		num_fds = epoll_wait(g_hCompPort, evts, SC_EPOLL_EVENTMAX, timeout);

		if(!g_bServerRunning)
			break;

//		Log(LOG_DEBUG, "epoll_wait ret: %x\n", num_fds);

		if(num_fds < 0)
		{
			int nError = WSAGetLastError();
			if(nError != EINTR)
			{
				Log(LOG_FATAL, "epoll_wait failed: %d, %s\n", nError, GetWsaErrorString(nError));
				break;
			}

			continue;
		}

		if(num_fds == 0)
		{
			// timer timeout
			timeout = g_core.OnTimeout();
			continue;
		}

		for(int i=0; i<num_fds; ++i)
		{
			IoEventListener* pListener = (IoEventListener*) evts[i].data.ptr;
			//printf("evt[%d].events=%x\n", i, evts[i].events);

			if(evts[i].events & EPOLLOUT)
			{
				pListener->IO_OnWritable();
			}

			if(evts[i].events & EPOLLIN)
			{
				pListener->IO_OnReadable();
			}

			if(evts[i].events & EPOLLERR)
			{
				pListener->IO_OnException();
			}
		}

		if(g_pollerWaker.m_bNeedReconfigTimer)
		{
			g_pollerWaker.m_bNeedReconfigTimer = false;
			timeout = g_core.ReconfigureTimers();
		}
		else if(timeout >= 0)
		{
			timeout = g_core.OnTimeout();
		}
	}
}

#endif

