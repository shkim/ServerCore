import java.net.*;
import java.io.*;
import java.util.*;

public class UdpEchoClient
{
	public static void main(String[] args)
	{
		String server = "localhost";
		int port = 10023;
		String msg = "This is a UDP echo test";

		try
		{
			byte[] udpMsg = msg.getBytes();
			InetAddress addr = InetAddress.getByName(server);
			
			DatagramPacket packet = new DatagramPacket(udpMsg,udpMsg.length,addr,port);
			DatagramSocket datagramSocket = new DatagramSocket();
			datagramSocket.send(packet);
      
			byte[] dataArray = packet.getData();
			for(int cnt = 0; cnt < packet.getLength(); cnt++)
				dataArray[cnt] = 'x';

			System.out.println(new String(packet.getData()));
			datagramSocket.receive(packet);
			System.out.println(new String(packet.getData()));
			datagramSocket.close();
		}
		catch(Exception ex)
		{
			ex.printStackTrace();
		}
	}
}