// THIS FILE WAS GENERATED BY "Binary Protocol Designer tool"
// Visit https://github.com/shkim/bpdtool for more information.
"use strict";
var net = require('net');
var util = require('util');

exports.PROTOCOL_MAGIC			= 0x1234;
exports.PROTOCOL_VERSION		= 1;

// Packet Stages
exports.STAGE_NOAUTH		=0; // NoAuth
exports.STAGE_AUTHED		=1; // Authed

// Client->Server Packet IDs:
var IDC2S_Hello				= 0;
var IDC2S_ReqNick			= 1;
var IDC2S_ReqChat			= 2;
var IDC2S_LastPacketID		= 2;

// Server->Client Packet IDs:
var IDS2C_Welcome			= 0;
var IDS2C_NoWelcome			= 1;
var IDS2C_SetNick			= 2;
var IDS2C_Chat				= 3;
var IDS2C_LastPacketID		= 3;

// Client should send this packet first
function recv_Hello(self, buf, offset)
{
	var pkt = new Object();

	pkt.magic = buf.readInt32LE(offset); offset += 4;

	pkt.version = buf.readInt32LE(offset);

	self.on_Hello(self, pkt);
}

function recv_ReqNick(self, buf, offset)
{
	var pkt = new Object();

	var len_nick = buf.readUInt8(offset); offset += 1;
	if (len_nick > 0)
	{
		pkt.nick = buf.toString(undefined, offset, offset + len_nick);
	}

	self.on_ReqNick(self, pkt);
}

function recv_ReqChat(self, buf, offset)
{
	var pkt = new Object();

	var len_msg = buf.readUInt16LE(offset); offset += 2;
	if (len_msg > 0)
	{
		pkt.msg = buf.toString(undefined, offset, offset + len_msg);
	}

	self.on_ReqChat(self, pkt);
}

var PacketDispatchTable;
var PacketDispatchTableStages = new Array(2);
PacketDispatchTableStages[exports.STAGE_NOAUTH] = new Array(IDC2S_LastPacketID +1)
PacketDispatchTableStages[exports.STAGE_NOAUTH][IDC2S_Hello] = recv_Hello;
PacketDispatchTableStages[exports.STAGE_AUTHED] = new Array(IDC2S_LastPacketID +1)
PacketDispatchTableStages[exports.STAGE_AUTHED][IDC2S_ReqNick] = recv_ReqNick;
PacketDispatchTableStages[exports.STAGE_AUTHED][IDC2S_ReqChat] = recv_ReqChat;

function processReceive(self, recvData)
{
	var buff;
	var buffLen = recvData.length;
	if (self._curRecvPtr > 0)
	{
		recvData.copy(self._recvBuffer, self._curRecvPtr);
		buff = self._recvBuffer;
		buffLen += self._curRecvPtr;
	}
	else
	{
		buff = recvData;
	}
	
	var remainLen;
	var basePtr = 0;
	for(;;)
	{
		var pktId = buff.readUInt8(basePtr);
		if (pktId > IDC2S_LastPacketID)
		{
			console.error("packet id range over: %d", pktId);
			self.kick();
			return;
		}
		
		remainLen = buffLen - basePtr;
		if (remainLen <= 3)
			break;
		
		var pktLen = buff.readUInt16LE(basePtr + 1);
		if (pktLen <= remainLen)
		{
			var deserialFn = PacketDispatchTable[pktId];
			if (!deserialFn)
			{
				console.error("Invalid packet id: %d", pktId);
				self.kick();
				return;
			}
			
			deserialFn(self, buff, basePtr + 3);
			basePtr += pktLen;
			
			if (basePtr == buffLen)
			{
				// consumed all received buffer
				self._curRecvPtr = 0;
				return;
			}
		}
		else if (pktLen > 2048)
		{
			console.error("Unsupported too long packet length: %d (id=%d)", pktLen, pktId);
			self.kick();
			return;
		}
		else
		{
			break;
		}
	}
	
	buff.copy(self._recvBuffer, 0, basePtr, buffLen);
	self._curRecvPtr = remainLen;
}

var isCheckedHandlers = false;

function Instance(sock)
{
	this._socket = sock;

	if(!isCheckedHandlers)
	{
		checkHandlerExists(this.on_Hello, 'Hello');
		checkHandlerExists(this.on_ReqNick, 'ReqNick');
		checkHandlerExists(this.on_ReqChat, 'ReqChat');
		isCheckedHandlers = true;
	}

	PacketDispatchTable = PacketDispatchTableStages[0];

	this._recvBuffer = new Buffer(8192);
	this._curRecvPtr = 0;

	var self = this;
		
	this._socket.on('data', function(recvData) {
		processReceive(self, recvData);
	});

	this._socket.on('close', function() {
		if (util.isFunction(self._onDisconnect))
			self._onDisconnect();
	});	
}

function checkHandlerExists(fn, pktName)
{
	if (!util.isFunction(fn))
	{
		throw new Error("bpdtool Error: Handler function for Packet '"+ pktName +"' not found.");
	}
}

Instance.prototype.setPacketDispatchStage = function(stage)
{
	if (stage >= 2)
		throw new Error("bpdtool Error: Packet dispatch stage number out of range: " + stage);

	PacketDispatchTable = PacketDispatchTableStages[stage];
}

Instance.prototype.kick = function()
{
	// user wants disconnect
	this._socket.destroy();
}

Instance.prototype.setOnDisconnect = function(cb)
{
	this._onDisconnect = cb;
}

exports.setPacketHandlers = function(pktCBs)
{
	for (var k in pktCBs)
	{
		Instance.prototype['on_'+k] = pktCBs[k];
	}
}

function sendPacket(self, pktId, buf, len)
{
	buf.writeUInt8(pktId, 0);
	buf.writeInt16LE(len, 1);

	if (buf.length == len)
		self._socket.write(buf);
	else
		self._socket.write(buf.slice(0,len));
}

// Hello packet accecpted
Instance.prototype.send_Welcome = function(user_count)
{
	var buf = new Buffer(7);
	buf.writeUInt8(IDS2C_Welcome, 0);
	buf.writeInt16LE(buf.length, 1);

	buf.writeInt32LE(user_count, 3);

	this._socket.write(buf);
}

// version not accepted, server will disconnect
Instance.prototype.send_NoWelcome = function(server_version)
{
	var buf = new Buffer(7);
	buf.writeUInt8(IDS2C_NoWelcome, 0);
	buf.writeInt16LE(buf.length, 1);

	buf.writeInt32LE(server_version, 3);

	this._socket.write(buf);
}

Instance.prototype.send_SetNick = function(nick)
{
	var len_nick = Buffer.byteLength(nick);
	var buf = new Buffer(5 + len_nick);

	var offset = 3;
	buf.writeUInt8(len_nick, offset); offset += 1;
	if(len_nick > 0)
	{
		offset += buf.write(nick, offset);
		buf.writeInt8(0, offset); offset += 1;
	}

	sendPacket(this, IDS2C_SetNick, buf, offset);
}

Instance.prototype.send_Chat = function(nick, msg)
{
	var len_nick = Buffer.byteLength(nick);
	var len_msg = Buffer.byteLength(msg);
	var buf = new Buffer(7 + len_nick + len_msg);

	var offset = 3;
	buf.writeUInt8(len_nick, offset); offset += 1;
	if(len_nick > 0)
	{
		offset += buf.write(nick, offset);
		buf.writeInt8(0, offset); offset += 1;
	}
	buf.writeUInt8(len_msg, offset); offset += 1;
	if(len_msg > 0)
	{
		offset += buf.write(msg, offset);
		buf.writeInt8(0, offset); offset += 1;
	}

	sendPacket(this, IDS2C_Chat, buf, offset);
}

exports.Instance = Instance;
// END OF GENERATION
