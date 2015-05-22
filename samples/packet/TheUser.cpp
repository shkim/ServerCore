#include "pktdemo.h"

void TheUser::OnRelease()
{
}

void TheUser::OnDisconnect()
{
	if (m_pRoom)
		m_pRoom->OnUserQuit(this);

	m_pSocket = NULL;
}

bool TheUser::OnConnect(ITcpSocket* pSocket, int nErrorCode)
{
	m_pSocket = pSocket;
	m_pPacketDispatchTable = s_aPacketDispatchTable[0];
	m_pRoom = NULL;
	return true;
}

unsigned int TheUser::OnReceive(char* pBuffer, unsigned int nLength)
{
	NetStreamReader _nsr(pBuffer);

_nextPacket:

	unsigned int nPacketID = _nsr._ReadByteAt(0);
	if (nPacketID > IDC2S_LastPacketID)
	{
	_invPacket:
		Log(LOG_WARN, "Invalid packet id: %d\nClosing client %s\n", nPacketID, m_pSocket->GetRemoteAddr());
		m_pSocket->Kick();
		return 0;
	}

	// To process a packet, the buffer should contain at least 3 bytes.
	if (nLength > 3)
	{
		unsigned int nPacketLen = _nsr._ReadWordAt(1);
		if (nPacketLen <= nLength)
		{
			if (m_pPacketDispatchTable[nPacketID] == NULL)
				goto _invPacket;

			_nsr.MapPacket(3, nPacketLen);
			if (false == (this->*m_pPacketDispatchTable[nPacketID])(_nsr))
			{
				// error
				m_pSocket->Kick();
				return 0;
			}

			nLength -= nPacketLen;

			if (nLength > 3)
			{
				// process more packet
				goto _nextPacket;
			}
		}
		else if (nPacketLen > 2048)
		{
			Log(LOG_WARN, "Invalid packet length:%d(pkid=%d)\nClosing client %s\n",
				nPacketLen, nPacketID, m_pSocket->GetRemoteAddr());

			m_pSocket->Kick();
			return 0;
		}
	}

	return (int)_nsr.GetOffsetFrom(pBuffer);
}

bool TheUser::On_Hello(PktHello* pkt)
{
	Log(LOG_INFO, "Got Hello, magic=%x, ver=%x\n", pkt->magic, pkt->version);
	if (pkt->magic != PROTOCOL_MAGIC)
	{
		return false;	// will close the connection
	}

	if (pkt->version == PROTOCOL_VERSION)
	{
		Send_Welcome(1);
		SetPacketDispatchStage(STAGE_AUTHED);
		return true;
	}
	else
	{
		Send_NoWelcome(PROTOCOL_VERSION);
		return false;
	}
}

bool TheUser::On_ReqNick(PktReqNick* pkt)
{
	Log(LOG_INFO, "ReqNick: %s\n", pkt->nick.psz);
	m_strNick = pkt->nick.psz;
	m_pRoom = &g_room;
	m_pRoom->OnUserJoin(this);

	return true;
}

bool TheUser::On_ReqChat(PktReqChat* pkt)
{
	Log(LOG_INFO, "ReqChat: %s\n", pkt->msg.psz);
	if (m_pRoom)
	{
		m_pRoom->Broadcast(m_strNick.c_str(), pkt->msg.psz);
		return true;
	}

	return false;
}