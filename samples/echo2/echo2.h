/*
	ServerCore Sample -- Echo 2 (Lined echo)
*/
#pragma once

#include "../samples.h"

class Client : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Client(); }

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual void OnRelease();
	virtual void OnFinalDestruct();
	virtual void OnSendBufferEmpty() {}
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);

private:
	ITcpSocket* m_pSocket;

	void HandlePacketString(const char* pszMsg);
};
