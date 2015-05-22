var net = require('net');
var util = require('util');

var theUser = require('./_TheUser');

var clis = new Set();

function onHello(self, pkt)
{
	if (pkt.magic != theUser.PROTOCOL_MAGIC)
	{
		console.log("Invalid magic");
		self.kick();
		return;
	}

	if (pkt.version != theUser.PROTOCOL_VERSION)
	{
		console.log("Invalid version: " + pkt.version);
		self.send_NoWelcome(theUser.PROTOCOL_VERSION);
		self.kick();
		return;
	}
	
	self.send_Welcome(clis.size);	
	self.setPacketDispatchStage(theUser.STAGE_AUTHED);
}

function broadcast(nick, msg)
{
	clis.forEach(function(cli) {
		cli.send_Chat(nick, msg);
	});
}

function onReqNick(self, pkt)
{
	if (!pkt.nick)
	{
		console.log("Empty nick not allowed.");
		self.kick();
		return;
	}
	
	self.nick = pkt.nick;
	
	broadcast("SYSTEM", "user <" + self.nick + "> joined.");
	clis.add(self);	
}

function onReqChat(self, pkt)
{
	broadcast(self.nick, pkt.msg);
}

theUser.setPacketHandlers({
	Hello: onHello,
	ReqNick: onReqNick,
	ReqChat: onReqChat
});

var userSerial = 0;

var server = net.createServer(function (sock) { //'connection' listener
    console.log('New client accepted.');

	var cli = new theUser.Instance(sock);
	cli.serial = ++userSerial;

	cli.setOnDisconnect(function() {
		console.log("Disconnected user: #" + cli.serial);
		if (cli.nick)
		{
			clis.delete(cli);
			broadcast("SYSTEM", "User <" + cli.nick + "> left.");
		}
	});
});

server.listen(6000, function () {
    console.log('Server is now listening...');
});



