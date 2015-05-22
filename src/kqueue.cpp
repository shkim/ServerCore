#include "stdafx.h"
#include "scimpl.h"

#ifdef __APPLE__

#define SC_KQUEUE_EVENTMAX		256

bool ServerCore::CreateIOCP()
{	
	ASSERT(g_hCompPort == INVALID_HANDLE_VALUE);

	g_hCompPort = kqueue();
	if(g_hCompPort < 0)
	{
		Log(LOG_FATAL, "kqueue: create failed (errno=%d)\n", errno);
		return false;
	}

	return true;
}

void ServerCore::CloseIOCP()
{
	if(g_hCompPort > 0)
	{
		close(g_hCompPort);
		g_hCompPort = 0;
	}
}

bool AssociateWithPoller(SOCKET hSocket, IoEventListener* pObject)
{
	struct kevent evt[1];
	
	evt[0].ident = hSocket;
	evt[0].filter = EVFILT_READ;// | EVFILT_WRITE;
	evt[0].flags = EV_ADD | EV_ENABLE;
	evt[0].fflags = 0;
	evt[0].data = 0;
	evt[0].udata = pObject;

	int ret = kevent(g_hCompPort, evt, 1, NULL, 0, NULL);	
	if(ret < 0)
	{		
		Log(LOG_FATAL, "kevent(add) failed: %d,%s", errno, GetWsaErrorString(errno));
		return false;
	}
	
	return true;
}

void SetSendableInterestWithPoller(SOCKET hSocket, IoEventListener* pListener, bool bNotifySendable)
{
	struct kevent evt[1];
	
	evt[0].ident = hSocket;
	evt[0].filter = EVFILT_READ | (bNotifySendable ? EVFILT_WRITE : 0);
	evt[0].flags = EV_ADD | EV_ENABLE;
	evt[0].fflags = 0;
	evt[0].data = 0;
	evt[0].udata = pListener;

	int ret = kevent(g_hCompPort, evt, 1, NULL, 0, NULL);	
	if(ret < 0)
	{		
		Log(LOG_FATAL, "kevent(modify) failed: %d,%s", errno, GetWsaErrorString(errno));
	}
}

//////////////////////////////////////////////////////////////////////////////

void CompletedIoQueue::EnterPoller()
{
	struct kevent evts[SC_KQUEUE_EVENTMAX];
	int num_fds;

	ASSERT(g_bServerRunning);
	while(g_bServerRunning)
	{
		ZeroMemory(&evts, sizeof(evts));
		num_fds = kevent(g_hCompPort, NULL, 0, evts, SC_KQUEUE_EVENTMAX, NULL);
		if(!g_bServerRunning)
			break;

		printf("kqueue ret: %d\n", num_fds);
		
		if(num_fds < 0)
		{
			int nError = WSAGetLastError();
			if(nError != EINTR)
			{
				Log(LOG_FATAL, "kevent failed: %d, %s\n", nError, GetWsaErrorString(nError));
				break;
			}
			
			continue;
		}
		
		for(int i=0; i<num_fds; ++i)
		{
			IoEventListener* pListener = (IoEventListener*) evts[i].udata;
			printf("evt[%d] kq filter: %d, ident=%ld\n", i, evts[i].filter, evts[i].ident);

			if(evts[i].flags & EV_ERROR)
			{
				printf("evts[%d]: error, filter=%d\n", i, evts[i].filter);
				pListener->IO_OnException();			
				continue;
			}

			if(evts[i].filter == EVFILT_READ)
			{
				pListener->IO_OnReadable();			
			}
			else if(evts[i].filter == EVFILT_WRITE)
			{
				pListener->IO_OnWritable();
			}
			else
			{
				printf("Unhandled kq filter: %d\n", evts[i].filter);
			}
		}
	}
}

#endif

