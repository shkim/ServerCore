#include "pktdemo.h"

/*
	Protocol Packet Serialization Sample
	with bpdtool, Binary Protocol Designer tool ( https://github.com/shkim/bpdtool )
	Files with prefix '_' are ALL GENERATED, DO NOT EDIT BY HAND, use bpdtool and re-generate.
*/

ChatRoom g_room;

ChatRoom::ChatRoom()
{
	m_cs.Create();
}

ChatRoom::~ChatRoom()
{
	size_t a;
	m_cs.Destroy();
}

void ChatRoom::Broadcast(const char* nick, const char* msg)
{
	m_cs.Lock();
	for (auto itr = m_users.begin(); itr != m_users.end(); ++itr)
	{
		TheUser* pUser = *itr;
		pUser->Send_Chat(nick, msg);
	}
	m_cs.Unlock();
}

void ChatRoom::OnUserJoin(TheUser* pUser)
{
	char msg[512];
	StringCchPrintfA(msg, 512, "New User <%s> joined", pUser->GetNick());
	Broadcast("SYSTEM", msg);

	m_cs.Lock();
	{
		m_users.insert(pUser);
	}
	m_cs.Unlock();
}

void ChatRoom::OnUserQuit(TheUser* pUser)
{
	m_cs.Lock();
	{
		m_users.erase(pUser);
	}
	m_cs.Unlock();

	char msg[512];
	StringCchPrintfA(msg, 512, "User <%s> left", pUser->GetNick());
	Broadcast("SYSTEM", msg);
}

class PacketSampleModule : public IServerModule
{
	virtual bool Create(IServerCore* pCore)
	{
		pCore->RegisterClientListener(TheUser::Creator);
		TheUser::SetupPacketDispatchTable();	// bpdtool guide: user must call this method on startup
		return true;
	}

	virtual void Destroy()
	{
	}
};

SC_SERVERMODULE_ENTRY(PacketSampleModule)
