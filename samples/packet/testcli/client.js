"use strict";
var readline = require('readline');
var theServer = require('./_TheServer');


var rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
});


function on_Welcome(self, pkt)
{
	console.log("onWelcome: " + pkt.user_count);
	
	rl.question("Nick? ", function(nick) {
		svr.send_ReqNick(nick);
		chatLoop();
	});
}

function on_NoWelcome(self, pkt)
{
	console.log("NoWelcome, server_version=%d", pkt.server_version);
	svr.kick();
}

function on_SetNick(self, pkt)
{
	console.log("My nick is now '%s'...", pkt.nick);
}

function on_Chat(self, pkt)
{
	console.log("%s: %s", pkt.nick, pkt.msg);
}

theServer.setPacketHandlers({
	Welcome: on_Welcome,
	NoWelcome: on_NoWelcome,
	SetNick: on_SetNick,
	Chat: on_Chat 
});

var svr = new theServer.Instance();
svr.setOnDisconnect(function() {
	console.log("Disconnected from server");
	rl.close();
});

svr.connect(6000, 'localhost', function() {
	svr.send_Hello(theServer.PROTOCOL_MAGIC, theServer.PROTOCOL_VERSION);
});


function chatLoop()
{
	rl.question("> ", function(msg) {
		if (msg.length == 0)
		{
			svr.kick();
			return;
		}
		
		svr.send_ReqChat(msg);
		chatLoop();
	});
}
