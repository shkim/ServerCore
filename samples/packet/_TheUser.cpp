// THIS FILE WAS GENERATED BY "Binary Protocol Designer"
// Visit https://github.com/shkim/bpdtool for more information.
#include "pktdemo.h"

TheUser::PacketReceiverFuncT TheUser::s_aPacketDispatchTable[2][3];

void TheUser::SetupPacketDispatchTable()
{
	memset(s_aPacketDispatchTable, 0, sizeof(s_aPacketDispatchTable));

	s_aPacketDispatchTable[STAGE_NOAUTH][IDC2S_Hello] = &TheUser::_Recv_Hello;

	s_aPacketDispatchTable[STAGE_AUTHED][IDC2S_ReqNick] = &TheUser::_Recv_ReqNick;
	s_aPacketDispatchTable[STAGE_AUTHED][IDC2S_ReqChat] = &TheUser::_Recv_ReqChat;
}

void TheUser::SetPacketDispatchStage(int stage)
{
	m_pPacketDispatchTable = s_aPacketDispatchTable[stage];
}

// Hello packet accecpted
void TheUser::Send_Welcome
(
	int user_count	// Number of chat users
)
{
	char _buff[32];
	NetStreamWriter _nsw(_buff, sizeof(_buff));

	_buff[0] = IDS2C_Welcome;
	_nsw.Skip(3);
	_nsw.WriteInt(user_count);

	m_pSocket->Send(_buff, _nsw.ClosePacket(_buff, 1));
}

// version not accepted, server will disconnect
void TheUser::Send_NoWelcome
(
	int server_version
)
{
	char _buff[32];
	NetStreamWriter _nsw(_buff, sizeof(_buff));

	_buff[0] = IDS2C_NoWelcome;
	_nsw.Skip(3);
	_nsw.WriteInt(server_version);

	m_pSocket->Send(_buff, _nsw.ClosePacket(_buff, 1));
}

void TheUser::Send_SetNick
(
	const char* nick
)
{
	char _buff[1056];
	NetStreamWriter _nsw(_buff, sizeof(_buff));

	_buff[0] = IDS2C_SetNick;
	_nsw.Skip(3);
	_nsw.WriteStringTiny(nick);

	m_pSocket->Send(_buff, _nsw.ClosePacket(_buff, 1));
}

void TheUser::Send_Chat
(
	const char* nick, 
	const char* msg
)
{
	char _buff[1056];
	NetStreamWriter _nsw(_buff, sizeof(_buff));

	_buff[0] = IDS2C_Chat;
	_nsw.Skip(3);
	_nsw.WriteStringTiny(nick);
	_nsw.WriteStringTiny(msg);

	m_pSocket->Send(_buff, _nsw.ClosePacket(_buff, 1));
}

// Client should send this packet first
bool TheUser::_Recv_Hello(NetStreamReader& _nsr)
{
	PktHello _pkt;

	_pkt.magic = _nsr.ReadInt();
	_pkt.version = _nsr.ReadInt();

	if(_nsr.IsValid())
		return On_Hello(&_pkt);

	return false;
}

bool TheUser::_Recv_ReqNick(NetStreamReader& _nsr)
{
	PktReqNick _pkt;

	_nsr.ReadStringTiny(&_pkt.nick);

	if(_nsr.IsValid())
		return On_ReqNick(&_pkt);

	return false;
}

bool TheUser::_Recv_ReqChat(NetStreamReader& _nsr)
{
	PktReqChat _pkt;

	_nsr.ReadString(&_pkt.msg);

	if(_nsr.IsValid())
		return On_ReqChat(&_pkt);

	return false;
}

// END OF GENERATION