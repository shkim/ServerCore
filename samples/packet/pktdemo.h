#include "../samples.h"
#include <string>
#include <set>

#include "TheUser.h"

class ChatRoom
{
public:
	ChatRoom();
	~ChatRoom();

	void Broadcast(const char* nick, const char* msg);
	void OnUserJoin(TheUser* pUser);
	void OnUserQuit(TheUser* pUser);

private:

	std::set<TheUser*> m_users;
	CriticalSection m_cs;
};

extern ChatRoom g_room;