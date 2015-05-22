/*
	ServerCore Sample -- Timer
*/
#pragma once

#include "../samples.h"

class Client : public BaseClientListenerWithTimer
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client(); }

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual void OnRelease();
	virtual void OnFinalDestruct();
	virtual void OnSendBufferEmpty() {}
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);
	
	virtual void OnTimer(int nTimerID);

private:
	ITcpSocket* m_pSocket;
	DWORD m_dwPrevTick;
	int m_nTimerCount;
};

