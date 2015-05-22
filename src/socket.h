#pragma once

SC_NAMESPACE_BEGIN

#define MAX_RECVBUFFER_SIZE			4096
//#define MAX_RECVBUFFER_SIZE			8192
#define IOCP_UDP_ITEMALLOC_SIZE		2048

class ServerPart;
class TcpSocket;
class UdpSocket;
struct TSendItem;
struct TUdpRwItem;

class ClientListenerPool
#ifndef SC_USE_INTELTBB
	: public ILocklessQueueListener
#endif
{
public:
	ClientListenerPool();

#ifdef _DEBUG
	~ClientListenerPool()
	{
		SC_ASSERT(m_aFrees == NULL);
#ifdef SC_USE_INTELTBB
		SC_ASSERT(m_aTbbPendings == NULL);
#else
		SC_ASSERT(m_aPendings == NULL);
#endif
	}
#endif

	int Register(ClientListenerCreatorFuncT pfn, const void* param);
	void RegisterUdp();
	bool Finalize();
	void Destroy();

	struct ListenerItem : public LocklessEntryT
	{
		ITcpClientListener* pListener;
	};

	void Discard(ServerPart* pSP, ListenerItem* pListener);
	ListenerItem* Get(ServerPart* pSP);

	inline LONG GetBusyCount() const { return m_nBusyObjectCount; }

private:
	LocklessEntryT* GetQueueNodeItem();
#ifndef SC_USE_INTELTBB
	virtual void RemoveQueueNodeItem(LocklessEntryT* pItemToFree);
	virtual void CopyQueueNodeData(const LocklessEntryT* pItem, void* param);
#endif

	ATOMIC32 m_nBusyObjectCount;
	int m_nListenerTypeCount;
	int m_nUdpListenerCount;

	enum
	{
		MAX_LISTENER_CREATORS = 12
	};

	struct
	{
		ClientListenerCreatorFuncT pfn;
		const void* param;
	} m_aListenerCreators[MAX_LISTENER_CREATORS];

	LocklessStack m_itempool;
	LocklessStack* m_aFrees;

#ifdef SC_USE_INTELTBB
	typedef tbb::concurrent_queue<ListenerItem*> TbbLocklessQueue;
	TbbLocklessQueue* m_aTbbPendings;
#else
	LocklessQueue* m_aPendings;
#endif
};

#ifndef SC_PLATFORM_POSIX

//////////////////////////////////////////////////////////////////////////////
// Windows IOCP:

#define OVTYPE_UNKNOWN				0

#define OVTYPE_READING				1
#define OVTYPE_WRITING				2
#define OVTYPE_ACCEPTING			3
#define OVTYPE_CONNECTING			4
#define OVTYPE_DISCONNECTING		5
#define OVTYPE_KICKING				6
#define OVTYPE_REQ_RECVBUFF			7

#define OVTYPE_SENDFILE				9

#define OVTYPE_UDP_READ				10
#define OVTYPE_UDP_WRITE			11

#define CK_TERMINATE_THREAD			1
#define CK_HELLO					2

//#define SC_SUPPORT_SENDFILE

struct OVERLAPPED_EXT : public OVERLAPPED
{
	LONG type;

	union
	{
		//TcpSocket* pObject;
		TcpSocket* pTcpSock;
		UdpSocket* pUdpSock;
	};

	union
	{
		char* pRecvBuffer;
		TSendItem* pSendItem;
		TUdpRwItem* pUdpItem;
	};
};

struct TSendItem
{
	OVERLAPPED_EXT ov;
	
	struct TSendItem* pNext;
	unsigned short len, sentlen;
	bool bSent;
	char buf[1];
};

struct TUdpRwItem
{
	OVERLAPPED_EXT ov;
	struct sockaddr_in sa;
	int sa_len;
	unsigned short recvlen;
	char buf[1];
};


class TcpSocket : public ITcpSocket
{
public:
	TcpSocket();

#ifdef _DEBUG
	~TcpSocket() { SC_ASSERT(m_hSocket == INVALID_SOCKET); }
#endif

	int Create();
	void Destroy();
	void Close();

	bool Accept(ServerPart* pPart);
	bool Connect(ITcpSocketListener* pLsnr, const char* pszServerAddr, unsigned short nRemotePort, unsigned short nLocalPort =0);
	void Disconnect();

	virtual void Send(const void* pData, int len);
	virtual void Kick(bool bCloseNow =false);
	virtual int GetPendingBytesToSend();
	virtual void SetCalledOnSendBufferEmpty(bool bWantCalledOnEmpty);
	virtual void RequestReceiveBuffer();
//	virtual void SendFile(HANDLE hFile, void* param);
	virtual const char* GetRemoteAddr(unsigned int* pIpAddr, unsigned short *pPortNum);
	virtual void GetLocalAddr(unsigned int* pIpAddr, unsigned short *pPortNum);
	virtual void SetSendFilter(ISocketStreamFilter* pFilter);

	static unsigned int __stdcall WorkerThreadProc(void *pv);

private:
	bool FlushPendingSend();
	bool PostRecv(WSABUF* pWB);
	void Reuse();
	void SafeDisconnect();

	void AfterAccept();
	void AfterConnect();
	void AfterDisconnect();
	void AfterKick();
	void OnSendComplete(TSendItem* pItem, DWORD dwSent);
	void OnRecvComplete(char* pBuffer, DWORD cbSize);
	void OnRequestRecvBuff();
	void _ProcessReceiveBufferRequest();
	void OnSendFileComplete();

	struct
	{
		LONG PendingBytesToSend;
		LONG Disconnected;
		LONG KickPosted;
		LONG SocketClosed;
		LONG ProcessingRecvReq;
		LONG ConnReset;	// connection reset by remote peer
		LONG RefCount;
#ifdef _DEBUG
		LONG RecvPostCount;
		LONG SendPostCount;
		LONG ConnPostCount;
#endif
	} m_atomic;

	__forceinline bool _IsDead()
	{
		return (m_atomic.Disconnected || m_atomic.KickPosted);
	}

	CriticalSection m_SendLock;

	SOCKET m_hSocket;
	OVERLAPPED_EXT m_ovConn;
	OVERLAPPED_EXT m_ovRecv;
	OVERLAPPED_EXT m_ovPostRecvReq;
#ifdef SC_SUPPORT_SENDFILE
	OVERLAPPED_EXT m_ovSendFile;
#endif

	ServerPart* m_pListenSvr;	// where hSocket came from (NULL if connect socket)
	ClientListenerPool::ListenerItem* m_pClientListenerItem;
	ITcpSocketListener* m_pSocketListener;
	ISocketStreamFilter* m_pSendFilter;

//	void* m_pWaitCompletionParam;
//	bool m_bWaitSendComplete;
//	bool m_bConnectSocketIocpAssociated;

	TSendItem* m_pFirstPendingSend;
	TSendItem* m_pLastPendingSend;
	int m_nPendingBufferCount;
	bool m_bCallOnSendBufferEmpty;
	bool m_bRecvIocpRequested;

	UINT m_nRecvBuffLen;
	UINT m_nRecvBuffStartPos;
	char m_recvbuff[MAX_RECVBUFFER_SIZE];

	char m_szIpAddress[16];
	struct sockaddr_in m_saRemoteAddr, m_saLocalAddr;

//	DWORD m_dwRecvd, m_dwRecvFlag;
	friend class SendBufferPool;
};

class UdpSocket : public IUdpSocket
{
public:
	UdpSocket(IUdpSocketListener* pLsnr);

#ifdef _DEBUG
	~UdpSocket() { SC_ASSERT(m_hSocket == INVALID_SOCKET); }
#endif

	bool CreateAndBind(ServerPart* pPart);
	void Close();

	virtual bool Send(const SockAddrT* pTo, const void* pData, int len);
	virtual void GetLocalAddr(unsigned int* pIpAddr, unsigned short *pPortNum);
	virtual void MakeRemoteAddr(unsigned int nRemoteIpAddr, unsigned short nRemotePort, SockAddrT* pOut);
	virtual void TranslateAddr(const SockAddrT* pSrc, unsigned int *pIpAddr, unsigned short *pPortNum);

	void PostRecv();
	void OnRecvComplete(TUdpRwItem* pItem, DWORD cbSize);
	void OnSendComplete(TUdpRwItem* pItem, DWORD cbSize);

private:
	static void CALLBACK SerializedUdpProcessor(ULONG_PTR dwParam);

	SOCKET m_hSocket;
	IUdpSocketListener* m_pSocketListener;

	// The address the server runs on
	struct sockaddr_in m_saLocalAddr;
};

#else // SC_PLATFORM_POSIX

//////////////////////////////////////////////////////////////////////////////
// UNIX (epoll,kqueue,select)

#ifdef _WIN32	// select() on win32
#define ECONNRESET		WSAECONNRESET
#define	EWOULDBLOCK		WSAEWOULDBLOCK
#define EINPROGRESS		WSAEINPROGRESS
#define SC_NEED_UNASSOCATION
typedef int socklen_t;
#endif

struct TSendItem// : LocklessEntryT
{
	struct TSendItem* pNext;

	int len;
	char buf[1];
};

struct TUdpRwItem
{
	struct sockaddr_in sa;
	int sa_len;
	unsigned short recvlen;
	char buf[1];
};

struct IoEventListener
{
	virtual void IO_OnReadable() =0;
	virtual void IO_OnWritable() =0;
	virtual void IO_OnException() =0;

//	virtual void IO_OnCanRead() =0;
//	virtual void IO_OnCanWrite() =0;
//	virtual void OnSocketReadable() =0;
//	virtual void OnSocketWritable() =0;
};

class TcpSocket : public ITcpSocket, public IoEventListener
{
public:
	TcpSocket();
	~TcpSocket();

	int Create();
	void Destroy();
	void Close();

	bool Accept(ServerPart* pPart, SOCKET hSocket, struct sockaddr_in& addr);
	bool Connect(ITcpSocketListener* pLsnr, const char* pszServerAddr, int nRemotePort, int nLocalPort);
	void Disconnect();

	virtual void Send(const void* pData, int len);
	virtual void Kick(bool bCloseNow =false);
	virtual int GetPendingBytesToSend();
	virtual void SetCalledOnSendBufferEmpty(bool bWantCalledOnEmpty);
	virtual void RequestReceiveBuffer();
	virtual void SetSendFilter(ISocketStreamFilter* pFilter);
	virtual const char* GetRemoteAddr(unsigned int* pIpAddr, unsigned short *pPortNum);
	virtual void GetLocalAddr(unsigned int *pIpAddr, unsigned short *pPortNum);
	
	static void* WorkerThreadProc(void *pv);

	inline SOCKET GetSocketHandle() { return m_hSocket; }

private:
	virtual void IO_OnReadable();
	virtual void IO_OnWritable();
	virtual void IO_OnException();

	void OnCanWrite();

	bool FlushPendingSend();

	void OnSendComplete(TSendItem* pItem, DWORD dwSent);
	void OnRecvComplete();
	void OnDisconnectComplete();

	struct
	{
		volatile ATOMIC32 Disconnected;
		volatile ATOMIC32 KickPosted;
		volatile ATOMIC32 SocketClosed;
		volatile ATOMIC32 ConnReset;	// connection reset by remote peer
		volatile ATOMIC32 RefCount;

		// posix (non-block) only:
		volatile ATOMIC32 LockRecvBuffer;
		volatile ATOMIC32 PendingRead;
	} m_atomic;

	SC_FORCEINLINE bool _IsKicked() const
	{
		return (m_atomic.KickPosted != 0);
	}

	SC_FORCEINLINE bool _IsDead()
	{
		//if(!AtomicCas(&m_atomic.Disconnected, 0,0))
		if(0 == _InterlockedCompareExchange(&m_atomic.Disconnected, 0,0))
			return true;	// disconnted == 1

		//if(!AtomicCas(&m_atomic.KickPosted, 0,0))
		if(0 == _InterlockedCompareExchange(&m_atomic.KickPosted, 0,0))
			return true;	// kickposted == 1

		return false;
	}

	SOCKET m_hSocket;			// remote peer
	ServerPart* m_pListenSvr;	// where hSocket came from (NULL if connect socket)

	CriticalSection m_SendLock;
	ClientListenerPool::ListenerItem* m_pClientListenerItem;
	ITcpSocketListener* m_pSocketListener;
	//ISocketListener* m_pRemoteListener;
	ISocketStreamFilter* m_pSendFilter;

	TSendItem* m_pFirstPendingSend;
	TSendItem* m_pLastPendingSend;
	int m_nPendingBufferCount;
	bool m_bSendable;

	//DWORD m_nIpAddress;
	char m_szIpAddress[16];
	struct sockaddr_in m_saLocalAddr, m_saRemoteAddr;

	unsigned int m_nRecvBuffLen;
	char m_recvbuff[MAX_RECVBUFFER_SIZE];

//	DWORD m_dwRecvd, m_dwRecvFlag;
	friend class SendBufferPool;
#ifdef _WIN32
	friend class CompletedIoQueue;
#endif
};

class UdpSocket : public IUdpSocket, public IoEventListener
{
public:
	UdpSocket(IUdpSocketListener* pLsnr);

#ifdef _DEBUG
	~UdpSocket() { SC_ASSERT(m_hSocket == INVALID_SOCKET); }
#endif

	bool CreateAndBind(ServerPart* pPart);
	void Close();

	virtual bool Send(const SockAddrT* pTo, const void* pData, int len);
	virtual void GetLocalAddr(unsigned int* pIpAddr, unsigned short *pPortNum);
	virtual void MakeRemoteAddr(unsigned int nRemoteIpAddr, unsigned short nRemotePort, SockAddrT* pOut);
	virtual void TranslateAddr(const SockAddrT* pSrc, unsigned int *pIpAddr, unsigned short *pPortNum);

	void PostRecv();
//	void OnRecvComplete(TUdpRwItem* pItem, DWORD cbSize);
//	void OnSendComplete(TUdpRwItem* pItem, DWORD cbSize);

private:
	virtual void IO_OnReadable();
	virtual void IO_OnWritable();
	virtual void IO_OnException();

	static void CALLBACK SerializedUdpProcessor(ULONG_PTR dwParam);

	SOCKET m_hSocket;
	IUdpSocketListener* m_pSocketListener;

	// The address the server runs on
	struct sockaddr_in m_saLocalAddr;
};

// completion event flow: from main-thread to worker-thread
enum CompletedIoTypes
{
	CIO_READ,
	CIO_WRITE,
	CIO_CONNECT,
	CIO_DISCONNECTED,
	CIO_TIMEOUT,
	CIO_ERROR,
	CIO_CORE_SHUTDOWN
};

struct CompletedIoInfo
{
	int type;
	int param;
	union
	{
		TcpSocket* pSocket;
		ITimerListener* pTimerListener;
	};
};

class CompletedIoQueue : ILocklessQueueListener
{
public:
	CompletedIoQueue();

	bool Create();
	void Destroy();
	void EnterPoller();

	void Get(CompletedIoInfo* pOut);
	void Put(int type, TcpSocket* p);
	void Put(ITimerListener* p, int nTimerID);
	void PutQuit();

private:
	struct QNodeItem : public LocklessEntryT
	{
		CompletedIoInfo data;
	};

	// ILocklessQueueListener impl:
	virtual LocklessEntryT* GetQueueNodeItem();
	virtual void RemoveQueueNodeItem(LocklessEntryT* pItemToFree);
	virtual void CopyQueueNodeData(const LocklessEntryT* pItem, void* param);

	ATOMIC32 m_nWaitCount;
	LocklessQueue m_queue;
	LocklessStack m_freeItems;
	CriticalSection m_cs;
	ConditionalVariable m_condivar;

	void _Put(QNodeItem* pItem);
};

class PollerWaker : public IoEventListener
{
public:
	PollerWaker();
	~PollerWaker();

	bool Create();
	void Destroy();
	void Wake();

	virtual void IO_OnReadable();
	virtual void IO_OnWritable();
	virtual void IO_OnException();

#ifdef _WIN32
	inline SOCKET GetWakerHandle() const { return m_hAcceptedSocket; }
#else
	inline int GetWakerHandle() const { return m_aPipes[0]; }
#endif

	bool m_bNeedReconfigTimer;

private:

#ifdef _WIN32
	SOCKET m_hConnectSocket;	// writer
	SOCKET m_hAcceptedSocket;	// reader
#else
	int m_aPipes[2];	// [0]=reader, [1]=writer
#endif
};

/*
// UtilitySocket makes the waiting io-loop wake up
class UtilitySocket : public IoEventListener
{
public:
	UtilitySocket();
	~UtilitySocket();

	bool Create();
	void Destroy();

	void SendEvent(int nEventID);
	void Clear();

	inline SOCKET GetPollingSocket() const { return m_hAcceptSocket; }
	inline bool IsSignaled() const { return m_bSignaled; }

private:
	virtual void OnSocketReadable();
	virtual void OnSocketWritable();

	SOCKET m_hServerSocket;
	SOCKET m_hClientSocket;
	SOCKET m_hAcceptSocket;

	bool m_bSignaled;
};
*/

bool MakeSocketNonBlock(SOCKET s, const char* desc);
bool AssociateWithPoller(SOCKET hSocket, IoEventListener* pListener);
void SetSendableInterestWithPoller(SOCKET hSocket, IoEventListener* pListener, bool bNotifySendable);

#if defined(__linux__) || defined(__APPLE__)
#define UnassociateWithPoller(_a,_b)
#else
void UnassociateWithPoller(SOCKET hSocket, IoEventListener* pListener);
#endif

//BOOL AssociateDeviceWithIocp(HANDLE hCompPort, HANDLE hDevice, void* ptr);

extern CompletedIoQueue g_cioq;
extern PollerWaker g_pollerWaker;

#endif //SC_PLATFORM_POSIX

//////////////////////////////////////////////////////////////////////////////
// Common

struct MemcpyFilter : public ISocketStreamFilter
{
	virtual void FilterNetworkStream(unsigned char* pDstFiltered, const unsigned char* pSrcRaw, int len);
};

class SendBufferPool
{
public:
	bool Create(int nPoolSize);

	TSendItem* Get(TcpSocket* pSender);
	void Discard(TSendItem* pItem);

	TUdpRwItem* Get(UdpSocket* pSender);
	void Discard(TUdpRwItem* pItem);

private:
	void IncTcpPool(int nCount);
	void IncUdpPool(int nCount);

	LocklessStack m_freeTcpItems;
	ATOMIC32 m_nTcpPoolSize;

	LocklessStack m_freeUdpItems;
	ATOMIC32 m_nUdpPoolSize;
};

extern SendBufferPool g_sbpool;
extern UINT g_nMaxUdpBufferSize;

SC_NAMESPACE_END
