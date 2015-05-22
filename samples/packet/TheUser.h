#pragma once

#include "netstream.h"
#include "_TheProtocol.h"

class ChatRoom;

class TheUser : public svrcore::BaseClientListener
{
public:
	static ITcpClientListener* Creator(const void*) { return new TheUser(); }
	virtual void OnFinalDestruct() { delete this; }

	inline const char* GetNick() const { return m_strNick.c_str(); }

#include "_TheUser.h.inl"

	virtual void OnRelease();
	virtual void OnDisconnect();
	virtual void OnSendBufferEmpty() {}
	virtual bool OnConnect(ITcpSocket* pSocket, int nErrorCode);
	virtual unsigned int OnReceive(char* pBuffer, unsigned int nLength);

private:
	std::string m_strNick;
	ChatRoom* m_pRoom;
};
