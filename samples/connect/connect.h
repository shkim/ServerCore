/*
	ServerCore Sample -- Connect
*/
#pragma once

#include "../samples.h"

class Client : public ITcpSocketListener
{
public:
	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);
	virtual void OnSendBufferEmpty() {}

private:
	ITcpSocket* m_pSocket;
};

class Dummy : public BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new Dummy(); }

	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual void OnDisconnect();
	virtual void OnRelease();
	virtual void OnFinalDestruct();
	virtual void OnSendBufferEmpty() {}
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);

private:
	ITcpSocket* m_pSocket;
};
