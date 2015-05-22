#ifndef __NETSTREAMRW_FOR_PROTOCOL_DESIGNER_H__
#define __NETSTREAMRW_FOR_PROTOCOL_DESIGNER_H__

//#define _NETSTREAM_USE_STL		// uncomment, if you need.

#ifndef ASSERT
#define ASSERT		SC_ASSERT
#endif

struct NsString
{
	int len;
	char* psz;
};

struct NsWideString
{
	int len;
	WCHAR* psz;
};

#ifdef _UNICODE
#define NsTString			NsWideString
#define ReadTString			ReadWideString
#define ReadTStringTiny		ReadWideStringTiny
#define WriteTString		WriteWideString
#define WriteTStringTiny	WriteWideStringTiny
#define WriteTStrings		WriteWideStrings
#define WriteTStringsTiny	WriteWideStringsTiny
#else
#define NsTString			NsString
#define ReadTString			ReadString
#define ReadTStringTiny		ReadStringTiny
#define WriteTString		WriteString
#define WriteTStringTiny	WriteStringTiny
#define WriteTStrings		WriteStrings
#define WriteTStringsTiny	WriteStringsTiny
#endif

struct NsBuffer
{
	int size;
	char* ptr;

	inline NsBuffer(void* p, int len)
	{
		ptr = (char*) p;
		size = len;
	}
};

union Ns16BitValue
{
	unsigned short v;
	unsigned char b[2];
};

union Ns32BitValue
{
	unsigned int v;
	unsigned char b[4];
};

template<typename T>
struct NsPrimitiveArray
{
	int count;
	T* items;

	inline char* _Alloc(int cnt, char* p)
	{
		count = cnt;
		items = (T*) p;
		return (char*) &items[cnt];
	}

	inline T& operator[](int i)
	{
		return items[i];
	}

#ifdef _NETSTREAM_USE_STL
	inline void CopyTo(std::vector<T>& vec)
	{
		if(count > 0)
		{
			vec.reserve(count);
			for(int i=0; i<count; i++)
				vec.push_back(items[i]);
		}
	}
#endif
};

template<typename T>
struct NsClassArray
{
	int count;
	T* items;

	inline ~NsClassArray()
	{
		for(int i=0; i<count; i++)
			items[i].~T();
	}

	inline char* _Alloc(int cnt, char* p)
	{
		count = cnt;
		items = (T*) p;

		for(int i=0; i<count; i++)
			new((void*)(&items[i])) T;

		return (char*) &items[cnt];
	}

	inline T& operator[](int i)
	{
		return items[i];
	}

#ifdef _NETSTREAM_USE_STL
	inline void CopyTo(std::vector<T>& vec)
	{
		if(count > 0)
		{
			vec.reserve(count);
			for(int i=0; i<count; i++)
				vec.push_back(items[i]);
		}
	}
#endif
};

class NetStreamReader
{
	char* m_pCurrent;
	char* m_pLimit;
	bool m_bValid;

public:
	inline NetStreamReader(char* p)
	{
		m_pCurrent = p;
		m_bValid = true;
	}

	inline unsigned char _ReadByteAt(int i)
	{
		return ((unsigned char) m_pCurrent[i]);
	}

	inline unsigned short _ReadWordAt(int i)
	{
		return *((unsigned short*)&m_pCurrent[i]);
	}

	inline unsigned short _ReadWordAt_BE(int i)
	{
		register Ns16BitValue tmp;
		tmp.b[1] = m_pCurrent[i];
		tmp.b[0] = m_pCurrent[i+1];
		return tmp.v;
	}

	inline void MapPacket(int cbHeader, int cbPacketLen)
	{
		m_pLimit = &m_pCurrent[cbPacketLen];
		m_pCurrent += cbHeader;
	}

	inline int GetOffsetFrom(char* p)
	{
		return (int)(m_pCurrent - p);
	}

	inline size_t GetAvailableSize()
	{
		return (m_pLimit - m_pCurrent);
	}

	inline char* GetCurrentPointer()
	{
		return m_pCurrent;
	}

	inline bool IsValid() const
	{
		return (m_bValid && m_pCurrent == m_pLimit);
	}

	inline char* PrepareDirectCast(int cb)
	{
		if((m_pLimit - m_pCurrent) == cb)
		{
			char * ret = m_pCurrent;
			m_pCurrent = m_pLimit;
			return ret;
		}

		return 0;
	}

	inline void SkipAll()
	{
		ASSERT(m_pCurrent < m_pLimit);
		m_pCurrent = m_pLimit;
	}

	inline char ReadChar()
	{
		char ret = *m_pCurrent;
		m_pCurrent++;
		return ret;
	}

	inline short ReadShort()
	{
		short ret = *((short*)m_pCurrent);
		m_pCurrent += sizeof(short);
		return ret;
	}

	inline int ReadInt()
	{
		int ret = *((int*)m_pCurrent);
		m_pCurrent += sizeof(int);
		return ret;
	}

	inline INT64 ReadInt64()
	{
		INT64 ret = *((INT64*)m_pCurrent);
		m_pCurrent += sizeof(INT64);
		return ret;
	}

	inline unsigned char ReadByte()
	{
		unsigned char ret = *m_pCurrent;
		m_pCurrent++;
		return ret;
	}

	inline unsigned short ReadWord()
	{
		unsigned short ret = *((unsigned short*)m_pCurrent);
		m_pCurrent += sizeof(short);
		return ret;
	}	

	inline unsigned short ReadWord_BE()
	{
		register Ns16BitValue tmp;
		tmp.b[1] = m_pCurrent[0];
		tmp.b[0] = m_pCurrent[1];
		m_pCurrent += sizeof(short);
		return tmp.v;
	}

	inline unsigned int ReadDword()
	{
		unsigned int ret = *((unsigned int*)m_pCurrent);
		m_pCurrent += sizeof(int);
		return ret;
	}

	inline unsigned int ReadDword_BE()
	{
		register Ns32BitValue tmp;
		tmp.b[3] = m_pCurrent[0];
		tmp.b[2] = m_pCurrent[1];
		tmp.b[1] = m_pCurrent[2];
		tmp.b[0] = m_pCurrent[3];
		m_pCurrent += sizeof(int);
		return tmp.v;
	}

	inline UINT64 ReadQword()
	{
		UINT64 ret = *((UINT64*)m_pCurrent);
		m_pCurrent += sizeof(UINT64);
		return ret;
	}

	inline float ReadFloat()
	{
		float ret = *((float*)m_pCurrent);
		m_pCurrent += sizeof(float);
		return ret;
	}

	inline double ReadDouble()
	{
		double ret = *((double*)m_pCurrent);
		m_pCurrent += sizeof(double);
		return ret;
	}

	inline bool ReadBool()
	{
		bool ret = *((bool*)m_pCurrent);
		m_pCurrent += sizeof(bool);
		return ret;
	}


	inline void ReadString(NsString* pRet)
	{
		pRet->len = ReadWord();
		if(pRet->len == 0)
		{
			pRet->psz = NULL;
		}
		else if(m_pCurrent + pRet->len < m_pLimit)
		{
			pRet->psz = m_pCurrent;
			m_pCurrent += pRet->len +1;

			if(pRet->psz[pRet->len] != 0)
				m_bValid = false;
		}
		else 
			m_bValid = false;
	}

	inline void ReadStringTiny(NsString* pRet)
	{
		pRet->len = ReadByte();
		if(pRet->len == 0)
		{
			pRet->psz = NULL;
		}
		else if(m_pCurrent + pRet->len < m_pLimit)
		{
			pRet->psz = m_pCurrent;
			m_pCurrent += pRet->len +1;

			if(pRet->psz[pRet->len] != 0)
				m_bValid = false;
		}
		else 
			m_bValid = false;
	}

	inline void ReadWideString(NsWideString* pRet)
	{
		pRet->len = ReadWord();
		if(pRet->len == 0)
		{
			pRet->psz = NULL;
		}
		else if(m_pCurrent + pRet->len * sizeof(WCHAR) < m_pLimit)
		{
			pRet->psz = (WCHAR*) m_pCurrent;
			m_pCurrent += (pRet->len + 1) * sizeof(WCHAR);

			if(pRet->psz[pRet->len] != 0)
				m_bValid = false;
		}
		else 
			m_bValid = false;
	}

	inline void ReadWideStringTiny(NsWideString* pRet)
	{
		pRet->len = ReadByte();
		if(pRet->len == 0)
		{
			pRet->psz = NULL;
		}
		else if(m_pCurrent + pRet->len * sizeof(WCHAR) < m_pLimit)
		{
			pRet->psz = (WCHAR*) m_pCurrent;
			m_pCurrent += (pRet->len + 1) * sizeof(WCHAR);

			if(pRet->psz[pRet->len] != 0)
				m_bValid = false;
		}
		else 
			m_bValid = false;
	}

#ifdef _NETSTREAM_USE_STL
	inline void ReadString(std::string* v)
	{
		int len = ReadWord();

		v->append(m_pCurrent, len);
		m_pCurrent += len+1;
	}

	inline void ReadStringTiny(std::string* v)
	{
		int len = ReadByte();

		v->append(m_pCurrent, len);
		m_pCurrent += len+1;
	}

	inline void ReadWideString(std::wstring* v)
	{
		int len = ReadWord();

		v->append((WCHAR*)m_pCurrent, len);
		m_pCurrent += len+1;
	}

	inline void ReadWideStringTiny(std::wstring* v)
	{
		int len = ReadByte();

		v->append((WCHAR*)m_pCurrent, len);
		m_pCurrent += len+1;

	}
#endif

	inline void ReadBuffer(NsBuffer* pRet)
	{
		pRet->size = ReadWord();
		pRet->ptr = m_pCurrent;
		m_pCurrent += pRet->size;
	}

	inline void ReadBufferTiny(NsBuffer* pRet)
	{
		pRet->size = ReadByte();
		pRet->ptr = m_pCurrent;
		m_pCurrent += pRet->size;
	}


	inline void ReadChars(char* pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count);
		m_pCurrent += count;
	}

	inline void ReadShorts(short *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(short));
		m_pCurrent += count * sizeof(short);
	}

	inline void ReadInts(int *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(int));
		m_pCurrent += count * sizeof(int);
	}

	inline void ReadInt64s(INT64 *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(INT64));
		m_pCurrent += count * sizeof(INT64);
	}

	inline void ReadBytes(unsigned char *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count);
		m_pCurrent += count;
	}

	inline void ReadWords(unsigned short *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(short));
		m_pCurrent += count * sizeof(short);
	}

	inline void ReadDwords(unsigned int *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(int));
		m_pCurrent += count * sizeof(int);
	}

	inline void ReadQwords(UINT64 *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(UINT64));
		m_pCurrent += count * sizeof(UINT64);
	}
		
	inline void ReadFloats(float *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(float));
		m_pCurrent += count * sizeof(float);
	}

	inline void ReadDoubles(double *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(double));
		m_pCurrent += count * sizeof(double);
	}

	inline void ReadBools(bool *pRets, int count)
	{
		memcpy(pRets, m_pCurrent, count * sizeof(bool));
		m_pCurrent += count * sizeof(bool);
	}

	inline void ReadStrings(NsString *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadString(&pRets[i]);
	}

	inline void ReadStringsTiny(NsString *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadString(&pRets[i]);
	}

	inline void ReadWideStrings(NsWideString *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadWideString(&pRets[i]);
	}

	inline void ReadWideStringsTiny(NsWideString *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadWideStringTiny(&pRets[i]);
	}

	inline void ReadBuffers(NsBuffer *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadBuffer(&pRets[i]);
	}

	inline void ReadBuffersTiny(NsBuffer *pRets, int count)
	{
		for(int i=0; i<count; i++)
			ReadBufferTiny(&pRets[i]);
	}
};


class NetStreamWriter
{
	char* m_pCurrent;
	char* m_pLimit;

public:
	inline NetStreamWriter(char* p, int size)
	{
		Reset(p, size);
	}

	inline void Reset(char* p, int size)
	{
		m_pCurrent = p;
		m_pLimit = &p[size];
	}

	inline char* GetCurrentPointer()
	{
		return m_pCurrent;
	}

	inline int GetOffsetFrom(char* p)
	{
		return (int)(m_pCurrent - p);
	}

	inline int ClosePacket(char* pBase, int cbIdSize)
	{
		return (*((unsigned short*)&pBase[cbIdSize]) = (unsigned short)(m_pCurrent - pBase));
	}

	inline void Skip(int n)
	{
		m_pCurrent += n;
	}

	inline static void _WriteWordAt(char* pWordPos, unsigned short v)
	{
		register Ns16BitValue tmp;
		tmp.v = v;
		pWordPos[0] = tmp.b[0];
		pWordPos[1] = tmp.b[1];
	}

	inline static void _WriteWordAt_BE(char* pWordPos, unsigned short v)
	{
		register Ns16BitValue tmp;
		tmp.v = v;
		pWordPos[0] = tmp.b[1];
		pWordPos[1] = tmp.b[0];
	}

	inline void WriteChar(char v)
	{
		*m_pCurrent++ = v;
	}

	inline void WriteByte(unsigned char v)
	{
		*((unsigned char*)m_pCurrent) = v;
		m_pCurrent += sizeof(char);
	}

	inline void WriteWord(unsigned short v)
	{
		*((unsigned short*)m_pCurrent) = v;
		m_pCurrent += sizeof(short);
	}

	inline void WriteWord_BE(unsigned short v)
	{
		register Ns16BitValue tmp;
		tmp.v = v;
		m_pCurrent[0] = tmp.b[1];
		m_pCurrent[1] = tmp.b[0];
		m_pCurrent += sizeof(short);
	}

	inline void WriteDword(unsigned int v)
	{
		*((unsigned int*)m_pCurrent) = v;
		m_pCurrent += sizeof(int);
	}

	inline void WriteDword_BE(unsigned int v)
	{
		register Ns32BitValue tmp;
		tmp.v = v;
		m_pCurrent[0] = tmp.b[3];
		m_pCurrent[1] = tmp.b[2];
		m_pCurrent[2] = tmp.b[1];
		m_pCurrent[3] = tmp.b[0];
		m_pCurrent += sizeof(int);
	}

#if 0	
	// for Non-x86 CPU
	inline void WriteShort(short v)
	{
		memcpy(m_pCurrent, &v, sizeof(short));
		m_pCurrent += sizeof(short);
	}

	inline void WriteInt(int v)
	{
		memcpy(m_pCurrent, &v, sizeof(int));
		m_pCurrent += sizeof(int);
	}

	inline void WriteInt64(INT64 v)
	{
		memcpy(m_pCurrent, &v, sizeof(INT64));
		m_pCurrent += sizeof(INT64);
	}

	inline void WriteQword(UINT64 v)
	{
		memcpy(m_pCurrent, &v, sizeof(UINT64));
		m_pCurrent += sizeof(UINT64);
	}

	inline void WriteFloat(float v)
	{
		memcpy(m_pCurrent, &v, sizeof(float));
		m_pCurrent += sizeof(float);
	}

	inline void WriteDouble(double v)
	{
		memcpy(m_pCurrent, &v, sizeof(double));
		m_pCurrent += sizeof(double);
	}
#else
	inline void WriteShort(short v)
	{
		*((short*)m_pCurrent) = v;
		m_pCurrent += sizeof(short);
	}

	inline void WriteInt(int v)
	{
		*((int*)m_pCurrent) = v;
		m_pCurrent += sizeof(int);
	}

	inline void WriteInt64(INT64 v)
	{
		*((INT64*)m_pCurrent) = v;
		m_pCurrent += sizeof(INT64);
	}

	inline void WriteQword(UINT64 v)
	{
		*((UINT64*)m_pCurrent) = v;
		m_pCurrent += sizeof(UINT64);
	}

	inline void WriteFloat(float v)
	{
		*((float*)m_pCurrent) = v;
		m_pCurrent += sizeof(float);
	}

	inline void WriteDouble(double v)
	{
		*((double*)m_pCurrent) = v;
		m_pCurrent += sizeof(double);
	}

#endif

	inline void WriteBool(bool v)
	{
		*((bool*)m_pCurrent) = v;
		m_pCurrent += sizeof(bool);
	}


	inline void Write(const void* p, int length)
	{
		ASSERT(p != NULL);
		memcpy(m_pCurrent, p, length);
		m_pCurrent += length;
		ASSERT(m_pCurrent < m_pLimit);
	}

	inline void WriteString(const char* v)
	{
		if(v == NULL || v[0] == 0)
		{
			WriteWord(0);
		}
		else
		{
			size_t len = strlen(v);
			ASSERT(len < 0xFFFF);
			WriteWord((unsigned short)len);
			Write(v, (int)len +1);
		}
	}

	inline void WriteStringTiny(const char* v)
	{
		if(v == NULL || v[0] == 0)
		{
			WriteByte(0);
		}
		else
		{
			size_t len = strlen(v);
			ASSERT(len < 0xFF);
			WriteByte((unsigned char)len);
			Write(v, (int)len +1);
		}
	}

#ifdef _WIN32
//#if (sizeof(WCHAR) == 2)
	#define _wstrlen	wcslen
#else
	// bpdtool assumes sizeof(WCHAR) is 2, but on Linux sizeof(wchar_t) is 4 and
	// wcslen accepts wchar_t*, so we can't use it with WCHAR*
	inline size_t _wstrlen(const WCHAR* p)
	{
		const WCHAR* p0 = p;
		while(*p != 0) p++;
		return (p - p0);
	}
#endif

	inline void WriteWideString(const WCHAR* v)
	{
		if(v == NULL || v[0] == 0)
		{
			WriteWord(0);
		}
		else
		{
			size_t len = _wstrlen(v);
			ASSERT(len < 0xFFFF);
			WriteWord((unsigned short)len);
			Write(v, ((int)len +1) * sizeof(WCHAR));
		}
	}

	inline void WriteWideStringTiny(const WCHAR* v)
	{
		if(v == NULL || v[0] == 0)
		{
			WriteByte(0);
		}
		else
		{
			size_t len = _wstrlen(v);
			ASSERT(len < 0xFF);
			WriteByte((unsigned char)len);
			Write(v, ((int)len +1) * sizeof(WCHAR));
		}
	}

	inline void WriteString(const NsString& v)
	{
		ASSERT(v.len < 0xFFFF);
		WriteWord(v.len);
		Write(v.psz, v.len +1);
	}

	inline void WriteStringTiny(const NsString& v)
	{
		ASSERT(v.len < 0xFF);
		WriteByte(v.len);
		Write(v.psz, v.len +1);
	}

	inline void WriteWideString(const NsWideString& v)
	{
		ASSERT(v.len < 0xFFFF);
		WriteWord(v.len);
		Write(v.psz, (v.len +1) * sizeof(WCHAR));
	}

	inline void WriteWideStringTiny(const NsWideString& v)
	{
		ASSERT(v.len < 0xFF);
		WriteByte(v.len);
		Write(v.psz, (v.len +1) * sizeof(WCHAR));
	}

#ifdef _NETSTREAM_USE_STL
	inline void WriteString(const std::string& v)
	{
		ASSERT(v.size() < 0xFFFF);
		WriteWord((unsigned short)v.size());
		Write(v.c_str(), (int)v.size() +1);
	}

	inline void WriteStringTiny(const std::string& v)
	{
		ASSERT(v.size() < 0xFF);
		WriteByte((unsigned char)v.size());
		Write(v.c_str(), (int)v.size() +1);
	}

	inline void WriteWideString(const std::wstring& v)
	{
		ASSERT(v.size() < 0xFFFF);
		WriteWord((unsigned short)v.size());
		Write(v.c_str(), (int)(v.size() +1) * sizeof(WCHAR));
	}

	inline void WriteWideStringTiny(const std::wstring& v)
	{
		ASSERT(v.size() < 0xFF);
		WriteByte((unsigned char)v.size());
		Write(v.c_str(), (int)(v.size() +1) * sizeof(WCHAR));
	}
#endif

	inline void WriteBuffer(const NsBuffer& v)
	{
		WriteWord(v.size);
		Write(v.ptr, v.size);
	}

	inline void WriteBufferTiny(const NsBuffer& v)
	{
		WriteByte(v.size);
		Write(v.ptr, v.size);
	}

	inline void WriteChars(const char *aValues, int count)
	{
		Write(aValues, count);
	}

	inline void WriteShorts(const short *aValues, int count)
	{
		Write(aValues, count * sizeof(short));
	}

	inline void WriteInts(const int *aValues, int count)
	{
		Write(aValues, count * sizeof(int));
	}

	inline void WriteInt64s(const INT64 *aValues, int count)
	{
		Write(aValues, count * sizeof(INT64));
	}

	inline void WriteBytes(const unsigned char *aValues, int count)
	{
		Write(aValues, count);
	}

	inline void WriteWords(const unsigned short *aValues, int count)
	{
		Write(aValues, count * sizeof(short));
	}

	inline void WriteDwords(const unsigned int *aValues, int count)
	{
		Write(aValues, count * sizeof(int));
	}

	inline void WriteQwords(const UINT64 *aValues, int count)
	{
		Write(aValues, count * sizeof(UINT64));
	}

	inline void WriteFloats(const float *aValues, int count)
	{
		Write(aValues, count * sizeof(float));
	}

	inline void WriteDoubles(const double *aValues, int count)
	{
		Write(aValues, count * sizeof(double));
	}

	inline void WriteBools(const bool *aValues, int count)
	{
		Write(aValues, count * sizeof(bool));
	}

	inline void WriteStrings(const char* *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteString(aValues[i]);
	}

	inline void WriteStringsTiny(const char* *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteStringTiny(aValues[i]);
	}

	inline void WriteWideStrings(const WCHAR* *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteWideString(aValues[i]);
	}

	inline void WriteWideStringsTiny(const WCHAR* *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteWideStringTiny(aValues[i]);
	}

#ifdef _NETSTREAM_USE_STL
	inline void WriteStrings(const std::string* aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteString(aValues[i]);
	}

	inline void WriteStringsTiny(const std::string* aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteStringTiny(aValues[i]);
	}

	inline void WriteWideStrings(const std::wstring* aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteWideString(aValues[i]);
	}

	inline void WriteWideStringsTiny(const std::wstring* aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteWideStringTiny(aValues[i]);
	}
#endif

	inline void WriteBuffers(const NsBuffer *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteBuffer(aValues[i]);
	}

	inline void WriteBuffersTiny(const NsBuffer *aValues, int count)
	{
		for(int i=0; i<count; i++)
			WriteBufferTiny(aValues[i]);
	}
};

#endif
