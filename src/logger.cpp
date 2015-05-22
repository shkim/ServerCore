#include "stdafx.h"
#include "scimpl.h"

#ifdef _WIN32
//#pragma warning(disable:4995)	// wsprintf ...
#pragma comment(lib, "shlwapi.lib")		// for PathAppend
#endif

SC_NAMESPACE_BEGIN

extern void EventLog_LogReport(LPCTSTR buff);

class LoggerCommon : public ILogger
{
public:
	virtual ~LoggerCommon() {}
	virtual bool Open() = 0;
	virtual void DisableLevel(int level) = 0;
};

class ConsoleLogger : public LoggerCommon
{
public:
	ConsoleLogger();
	virtual ~ConsoleLogger();

	virtual bool Open() { return true; }
	virtual void DisableLevel(int level);
	virtual void LogMessage(int& loguid, int level, const char* format, ...);
	virtual void LogMessage(int& loguid, int level, const WCHAR* format, ...);

private:
	char m_aLevelDisabled[16];
#ifdef _WIN32
	WORD m_aTextAttrs[16];
#else
	void MakeTermCode(int level, int ci);
	struct TermCode
	{
		BYTE len;
		char str[16];
	} m_aTextAttrs[16];
#endif
};

class AbstractFile
{
public:
	AbstractFile();

	bool IsValid();
	void Close();
	void Create(const char* filename);
	void Write(const void* pData, int cbLength);
	DWORD GetFileSize();

	static void RenameFile(const char* from, const char* to);
	static bool MakeDirectory(const char* dir);

private:
#ifdef _WIN32
	HANDLE m_hFile;
#else
	FILE* m_fp;
#endif
};

class LogFile
{
public:
	LogFile(const char* pszFilename);
	~LogFile();

	bool Open();
	void Close();
	void Rotate(ATOMIC32 cbRotateSize);
	void Write(const char* msg, int len);
	void Write(const WCHAR* msg, int len);
	void GetTimestampFilename(char* pOut, bool isExceptional);

	AbstractFile m_file;
	volatile ATOMIC32 m_cbFileSize;

	LogFile* x_pNext;
	char* m_pszBasename;

private:
	CriticalSection m_cs;
};

class FileLogger : public LoggerCommon
{
public:
	FileLogger();
	virtual ~FileLogger();

	void Close();
	bool SetLevelFile(int level, const char* pszFilename);
	void SetFileRotateSize(int nSizeInBytes);

	virtual bool Open();
	virtual void DisableLevel(int level);
	virtual void LogMessage(int& loguid, int level, const char* format, ...);
	virtual void LogMessage(int& loguid, int level, const WCHAR* format, ...);

private:
	ATOMIC32 m_nRotateSize;
	LogFile* m_aLogFiles[16];	// FIXME
	LogFile* m_pUniqueFileList;
};

class RemoteLoggerTODO : public LoggerCommon
{
public:
	virtual void LogMessage(int& loguid, int level, const char* format, ...);
	virtual void LogMessage(int& loguid, int level, const WCHAR* format, ...);
};

/*
class LogManager
{
public:
	LogManager();
	virtual ~LogManager();

	bool Create();
	void Destroy();
	void Assert(const char* eval, const char* file, int line);

	//	void OnLogClose(BaseLogger* pLogger);

	ILogger* GetDefaultLogger();
	void SetDefaultLogger(ILogger* pLogger);
	ConsoleLogger* CreateConsoleLogger();
	FileLogger* CreateFileLogger();
	//virtual RemoteLogger* CreateRemoteLogger(const char* pszLogServerAddr, int nPort);

private:
	ILogger* m_pDefaultLogger;
	CriticalSection m_cs;
	std::set<ILogger*> m_loggers;
	//bool m_bEnableMiniDump;
};
*/

















#ifdef _WIN32

static HANDLE s_hStdOut = NULL;

ConsoleLogger::ConsoleLogger()
{
	s_hStdOut = GetStdHandle(STD_ERROR_HANDLE);

	for (int i = 0; i<16; i++)
	{
		m_aLevelDisabled[i] = FALSE;
		m_aTextAttrs[i] = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
	}

	m_aTextAttrs[LOG_VERBOSE] = FOREGROUND_INTENSITY;
	m_aTextAttrs[LOG_DEBUG] = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
	m_aTextAttrs[LOG_INFO] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	m_aTextAttrs[LOG_WARN] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	m_aTextAttrs[LOG_ERROR] = FOREGROUND_RED | FOREGROUND_INTENSITY;
	m_aTextAttrs[LOG_FATAL] = FOREGROUND_RED | FOREGROUND_INTENSITY | COMMON_LVB_UNDERSCORE;
	m_aTextAttrs[LOG_SYSTEM] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
}

ConsoleLogger::~ConsoleLogger()
{
}

#else

enum ColorIndices
{
	CI_BLACK = 0,
	CI_RED,
	CI_GREEN,
	CI_BROWN,
	CI_BLUE,
	CI_MAGENTA,
	CI_CYAN,
	CI_GRAY,

	CI_DARK_GRAY,
	CI_LIGHT_RED,
	CI_LIGHT_GREEN,
	CI_YELLOW,
	CI_LIGHT_BLUE,
	CI_LIGHT_MAGENTA,
	CI_LIGHT_CYAN,
	CI_WHITE
};

void ConsoleLogger::MakeTermCode(int level, int ci)
{
	ASSERT(level < 16);

	int c1 = (ci <= 7) ? 22 : 1;
	int c2 = 30 + (ci & 7);

	sprintf(m_aTextAttrs[level].str, "\033[%02d;%dm", c1, c2);
	m_aTextAttrs[level].len = (BYTE)strlen(m_aTextAttrs[level].str);
}

ConsoleLogger::ConsoleLogger()
{
	for (int i = 0; i<16; i++)
	{
		m_aLevelDisabled[i] = FALSE;
		MakeTermCode(i, CI_GRAY);
	}

	MakeTermCode(LOG_DEBUG, CI_DARK_GRAY);
	MakeTermCode(LOG_WARN, CI_YELLOW);
	MakeTermCode(LOG_ERROR, CI_LIGHT_RED);
	MakeTermCode(LOG_FATAL, CI_LIGHT_RED);	// TODO: under score?
	MakeTermCode(LOG_INFO, CI_WHITE);
}

ConsoleLogger::~ConsoleLogger()
{
}

#endif

static const char* number_chars = "0123456789";

static void Print02d(char*& buf, int num, int chNext)
{
	buf[0] = number_chars[(num / 10) % 10];
	buf[1] = number_chars[num % 10];
	buf[2] = chNext;
	buf += 3;
}

static void Print02d(WCHAR*& buf, int num, int chNext)
{
	buf[0] = number_chars[(num / 10) % 10];
	buf[1] = number_chars[num % 10];
	buf[2] = chNext;
	buf += 3;
}

void ConsoleLogger::LogMessage(int& loguid, int level, const char* format, ...)
{
	char buffer[1024];
	char *p = buffer;
	va_list ap;
	int len;

	if (m_aLevelDisabled[level & 0xF])
		return;

	if (level & LOG_DATE)
	{
		level &= 0xF;

#ifdef _WIN32
		SYSTEMTIME tm;
		GetLocalTime(&tm);
		Print02d(p, tm.wMonth, '-');
		Print02d(p, tm.wDay, ' ');
		Print02d(p, tm.wHour, ':');
		Print02d(p, tm.wMinute, ':');
		Print02d(p, tm.wSecond, ' ');
//		StringCchPrintfA(buffer, 1024, "%02d-%02d %02d:%02d:%02d ",
//			tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
#else
		time_t tt = time(NULL);
		struct tm* lt = localtime(&tt);
		Print02d(p, lt->tm_mon + 1, '-');
		Print02d(p, lt->tm_mday, ' ');
		Print02d(p, lt->tm_hour, ':');
		Print02d(p, lt->tm_min, ':');
		Print02d(p, lt->tm_sec, ' ');
#endif
	}

	va_start(ap, format);
	StringCchVPrintfA(p, 1024 - 15, format, ap);
	len = (int)strlen(buffer);
	va_end(ap);

#ifdef _WIN32
	DWORD dwWritten;
	SetConsoleTextAttribute(s_hStdOut, m_aTextAttrs[level]);
	WriteConsoleA(s_hStdOut, buffer, len, &dwWritten, NULL);
#else
	write(0, m_aTextAttrs[level].str, m_aTextAttrs[level].len);
	write(0, buffer, len);
#endif
}

#ifdef _WIN32
#define _wstrlen wcslen
#endif

void ConsoleLogger::LogMessage(int& loguid, int level, const WCHAR* format, ...)
{
	WCHAR buffer[1024];
	WCHAR *p = buffer;
	va_list ap;
	int len;

	if (m_aLevelDisabled[level & 0xF])
		return;

	if (level & LOG_DATE)
	{
		level &= 0xF;

#ifdef _WIN32
		SYSTEMTIME tm;
		GetLocalTime(&tm);
		Print02d(p, tm.wMonth, '-');
		Print02d(p, tm.wDay, ' ');
		Print02d(p, tm.wHour, ':');
		Print02d(p, tm.wMinute, ':');
		Print02d(p, tm.wSecond, ' ');
#else
		time_t tt = time(NULL);
		struct tm* lt = localtime(&tt);
		Print02d(p, lt->tm_mon + 1, '-');
		Print02d(p, lt->tm_mday, ' ');
		Print02d(p, lt->tm_hour, ':');
		Print02d(p, lt->tm_min, ':');
		Print02d(p, lt->tm_sec, ' ');
#endif
	}

	va_start(ap, format);
	StringCchVPrintfW(p, 1024 - 15, format, ap);
	len = (int)_wstrlen(buffer);
	va_end(ap);

#ifdef _WIN32
	DWORD dwWritten;
	SetConsoleTextAttribute(s_hStdOut, m_aTextAttrs[level]);
	WriteConsoleW(s_hStdOut, buffer, len, &dwWritten, NULL);
#else
	write(0, m_aTextAttrs[level].str, m_aTextAttrs[level].len);
	write(0, buffer, len * sizeof(WCHAR));
#endif
}

void ConsoleLogger::DisableLevel(int level)
{
	m_aLevelDisabled[level & 0x0F] = true;
}













#ifdef _WIN32

AbstractFile::AbstractFile()
{
	m_hFile = INVALID_HANDLE_VALUE;
}

bool AbstractFile::IsValid()
{
	return (m_hFile != INVALID_HANDLE_VALUE);
}

void AbstractFile::Close()
{
	SC_ASSERT(IsValid());

	FlushFileBuffers(m_hFile);
	CloseHandle(m_hFile);
	m_hFile = INVALID_HANDLE_VALUE;
}

void AbstractFile::Create(const char* filename)
{
	m_hFile = CreateFile(filename, GENERIC_WRITE,
		FILE_SHARE_READ, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, NULL);
}

void AbstractFile::RenameFile(const char* from, const char* to)
{
	MoveFile(from, to);
}

bool AbstractFile::MakeDirectory(const char* dir)
{
	return CreateDirectory(dir, NULL) != FALSE;
}

void AbstractFile::Write(const void* pData, int cbLength)
{
	DWORD dwWritten;
	WriteFile(m_hFile, pData, cbLength, &dwWritten, NULL);
}

DWORD AbstractFile::GetFileSize()
{
	return ::GetFileSize(m_hFile, NULL);
}

#else

AbstractFile::AbstractFile()
{
	m_fp = NULL;
}

bool AbstractFile::IsValid()
{
	return (m_fp != NULL);
}

void AbstractFile::Close()
{
	ASSERT(IsValid());

	fflush(m_fp);
	fclose(m_fp);
	m_fp = NULL;
}

void AbstractFile::Create(const char* filename)
{
	m_fp = fopen(filename, "wb");
}

void AbstractFile::Write(const void* pData, int cbLength)
{
	fwrite(pData, cbLength, 1, m_fp);
}

DWORD AbstractFile::GetFileSize()
{
	fseek(m_fp, 0, SEEK_END);
	DWORD dwFileSize = ftell(m_fp);
	rewind(m_fp);

	return dwFileSize;
}

void AbstractFile::RenameFile(const char* from, const char* to)
{
	rename(from, to);
}

bool AbstractFile::MakeDirectory(const char* dir)
{
	return mkdir(dir, 0022);
}

#endif

static int _PrintCurrentTimeStamp(char* dest)
{
#ifdef _WIN32
	SYSTEMTIME tm;
	GetLocalTime(&tm);

	StringCchPrintfA(dest, 16, "%04d%02d%02d_%02d%02d%02d",
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
#else
	struct tm* pTM = localtime(NULL);	// FIXME

	StringCchPrintfA(dest, 16, "%04d%02d%02d_%02d%02d%02d",
		pTM->tm_year, pTM->tm_mon, pTM->tm_mday,
		pTM->tm_hour, pTM->tm_min, pTM->tm_sec);
#endif

	return (4 + 2 + 2 + 1 + 2 + 2 + 2);
}

//////////////////////////////////////////////////////////////////////

LogFile::LogFile(const char* pszFilename)
{
	m_cbFileSize = 0;
	x_pNext = NULL;

	size_t len = strlen(pszFilename) + 1;
	m_pszBasename = (char*)malloc(len);
	memcpy(m_pszBasename, pszFilename, len);

	m_cs.Create();
}

LogFile::~LogFile()
{
	if (m_file.IsValid())
		Close();

	free(m_pszBasename);
	m_cs.Destroy();
}

void LogFile::Close()
{
	char from_name[MAX_PATH];
	char to_name[MAX_PATH];

	StringCchPrintfA(from_name, MAX_PATH, "%s.log", m_pszBasename);
	GetTimestampFilename(to_name, false);

	m_file.Close();
	AbstractFile::RenameFile(from_name, to_name);
}

void LogFile::Rotate(ATOMIC32 cbRotateSize)
{
	char buff[MAX_PATH];
	StringCchPrintfA(buff, MAX_PATH, "%s.log", m_pszBasename);

	m_cs.Lock();
	if (m_cbFileSize > cbRotateSize)
	{
		Close();

		m_file.Create(buff);
		m_cbFileSize = 0;
	}
	m_cs.Unlock();
}

void LogFile::GetTimestampFilename(char* pOut, bool isExceptional)
{
#ifdef _WIN32
	SYSTEMTIME tm;
	GetLocalTime(&tm);

	StringCchPrintfA(pOut, MAX_PATH, "%s_%04d%02d%02d_%02d%02d%02d%s.log",
		m_pszBasename, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
		isExceptional ? "_ex" : "");
#else
	struct tm* pTM = localtime(NULL);	// FIXME

	StringCchPrintfA(pOut, MAX_PATH, "%s_%04d%02d%02d_%02d%02d%02d%s.log",
		m_pszBasename, pTM->tm_year, pTM->tm_mon, pTM->tm_mday, pTM->tm_hour, pTM->tm_min, pTM->tm_sec,
		isExceptional ? "_ex" : "");
#endif
}

// TODO: unix port
bool LogFile::Open()
{
	char buff[MAX_PATH];
	char final_filename[MAX_PATH];

	if (m_file.IsValid())
		return true;	// already opened

	// check if directory exist
	{
		StringCchCopyA(buff, MAX_PATH, m_pszBasename);

#ifdef _WIN32
		// replace '/' -> '\\'
		for (char* p = buff; *p != 0; p++)
		{
			if (*p == '/')
				*p = '\\';
		}
#endif

		char* pSlash = strrchr(buff, '\\');
		if (pSlash != NULL)
		{
			*pSlash = '\0';
			if (_access(buff, 0) < 0)
			{
				std::deque<std::string> dirstack;
				dirstack.push_back(buff);
				while (!dirstack.empty())
				{
					std::string& dir = dirstack.front();
					if (AbstractFile::MakeDirectory(dir.c_str()))
					{
						// directory successfully created
						printf("Directory created: %s\n", dir.c_str());
						dirstack.pop_front();
					}
					else
					{
						// couldn't create directory
						StringCchCopyA(buff, MAX_PATH, dir.c_str());
						pSlash = strrchr(buff, '\\');
						if (pSlash != NULL)
						{
							*pSlash = '\0';
							dirstack.push_front(buff);
						}
						else
						{
							// give up
							break;
						}
					}
				}
			}
		}
	}

	SC_ASSERT(!m_file.IsValid());

	StringCchPrintfA(buff, MAX_PATH, "%s.log", m_pszBasename);
	m_file.Create(buff);

	if (!m_file.IsValid())
	{
		printf("Cannot open log file: %s\n", buff);
		return false;
	}

	DWORD dwFileSize = m_file.GetFileSize();

	if (dwFileSize > 0)
	{
		// close, rename, reopen
		m_file.Close();
		GetTimestampFilename(final_filename, true);
		AbstractFile::RenameFile(buff, final_filename);
		m_file.Create(buff);
	}

	if (m_file.IsValid())
	{
#ifdef _WIN32
		SYSTEMTIME tm;
		GetLocalTime(&tm);
		printf("%04d-%02d-%02d %02d:%02d:%02d - Log file opened: %s\n",
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond, buff);
#else
		struct tm* pTM = localtime(NULL);	// FIXME
		printf("%04d-%02d-%02d %02d:%02d:%02d - Log file opened: %s\n",
			pTM->tm_year, pTM->tm_mon, pTM->tm_mday, pTM->tm_hour, pTM->tm_min, pTM->tm_sec, buff);
#endif

		return true;
	}

	return false;
}

void LogFile::Write(const char* msg, int len)
{
	m_file.Write(msg, len);
}

void LogFile::Write(const WCHAR* msg, int len)
{
	m_file.Write(msg, len * sizeof(WCHAR));
}

//////////////////////////////////////////////////////////////////////////

//static int s_nMaxLogFileSize;

FileLogger::FileLogger()
{
	m_pUniqueFileList = NULL;
	memset(m_aLogFiles, 0, sizeof(m_aLogFiles));
	m_nRotateSize = 10 * 1024 * 1024;	// 10MB
}

FileLogger::~FileLogger()
{
	SC_ASSERT(m_pUniqueFileList == NULL);
}

void FileLogger::SetFileRotateSize(int nSizeInBytes)
{
	if (nSizeInBytes > 1024)
	{
		m_nRotateSize = nSizeInBytes;
	}
}

bool FileLogger::SetLevelFile(int level, const char* pszFilename)
{
	LogFile* pLogFile = m_pUniqueFileList;
	for (; pLogFile != NULL; pLogFile = pLogFile->x_pNext)
	{
		if (_stricmp(pszFilename, pLogFile->m_pszBasename) == 0)
		{
			break;
		}
	}

	if (pLogFile == NULL)
	{
		pLogFile = new LogFile(pszFilename);
		if (pLogFile == NULL)
		{
			return false;
		}

		pLogFile->x_pNext = m_pUniqueFileList;
		m_pUniqueFileList = pLogFile;
	}

	if (level < 0)
	{
		for (int i = 0; i<16; i++)
			m_aLogFiles[i] = pLogFile;
	}
	else
	{
		m_aLogFiles[level] = pLogFile;
	}

	return true;
}

bool FileLogger::Open()
{
	if (m_pUniqueFileList == NULL)
	{
		return false;
	}

	for (int i = 0; i<16; i++)
	{
		if (m_aLogFiles[i] == NULL)
			continue;

		if (!m_aLogFiles[i]->Open())
		{
			return false;
		}
	}

	return true;
}

void FileLogger::Close()
{
	LogFile* pNextFile;
	for (LogFile* pLogFile = m_pUniqueFileList; pLogFile != NULL; pLogFile = pNextFile)
	{
		pNextFile = pLogFile->x_pNext;
		delete pLogFile;
	}

	m_pUniqueFileList = NULL;
}

void FileLogger::DisableLevel(int level)
{
	m_aLogFiles[level & 0x0F] = NULL;
}

void FileLogger::LogMessage(int& loguid, int level, const char* format, ...)
{
	char buffer[1024];
	char *p = buffer;
	va_list ap;
	int len;

	LogFile* pLogFile = m_aLogFiles[level & 0xF];
	if (pLogFile == NULL)
		return;

	if (level & LOG_DATE)
	{
		level &= 0xF;

#ifdef _WIN32
		SYSTEMTIME tm;
		GetLocalTime(&tm);
		Print02d(p, tm.wMonth, '-');
		Print02d(p, tm.wDay, ' ');
		Print02d(p, tm.wHour, ':');
		Print02d(p, tm.wMinute, ':');
		Print02d(p, tm.wSecond, ' ');

		//		StringCchPrintfA(buffer, 1024, "%02d-%02d %02d:%02d:%02d ",
		//			tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
#else
		time_t tt = time(NULL);
		struct tm* lt = localtime(&tt);
		Print02d(p, lt->tm_mon + 1, '-');
		Print02d(p, lt->tm_mday, ' ');
		Print02d(p, lt->tm_hour, ':');
		Print02d(p, lt->tm_min, ':');
		Print02d(p, lt->tm_sec, ' ');
#endif
	}

	va_start(ap, format);
	StringCchVPrintfA(p, 1024 - 16, format, ap);
	len = (int)strlen(buffer);
	va_end(ap);

#ifdef _WIN32
	if (buffer[len - 1] == '\n')
	{
		buffer[len - 1] = '\r';
		buffer[len] = '\n';
		buffer[++len] = 0;
	}
#endif

_writeAgain:
	if (_InterlockedExchangeAdd(&pLogFile->m_cbFileSize, len) < m_nRotateSize)
	{
		pLogFile->Write(buffer, len);
	}
	else
	{
		pLogFile->Rotate(m_nRotateSize);
		goto _writeAgain;
	}
}

void FileLogger::LogMessage(int& loguid, int level, const WCHAR* format, ...)
{

}












struct LogLevelInfo
{
	const char* name;
	int level;
};

static LogLevelInfo s_aLevelNames[] =
{
	{ "VERBOSE", LOG_VERBOSE },
	{ "DEBUG", LOG_DEBUG },
	{ "INFO", LOG_INFO },
	{ "WARN", LOG_WARN },
	{ "ERROR", LOG_ERROR },
	{ "FATAL", LOG_FATAL },
	{ "SYSTEM", LOG_SYSTEM },
	{ 0 }
};

bool LogOpen()
{
	int nDumpLevel;
	LoggerCommon* pLogger = NULL;
	ConsoleLogger* pConsoleLogger = NULL;
	FileLogger* pFileLogger = NULL;
	char buff[MAX_PATH];

	SC_ASSERT(__svrsideProxy.pLogger == NULL);

	IConfigSection* pLogConfig = g_core.GetConfigSection("Log");
	if (pLogConfig == NULL)
	{
		puts("No [Log] section. use default Console log method.");
		pConsoleLogger = new ConsoleLogger();
	}
	else
	{
		nDumpLevel = pLogConfig->GetInteger("MiniDumpLevel", 1);

		const char* method = pLogConfig->GetString("LogMethod");
		if (method == NULL)
		{
			puts("LogMethod directive not found in [Log] section.");
			return false;
		}

		if (_stricmp(method, "console") == 0)
		{
			pConsoleLogger = new ConsoleLogger();
		}
		else if (_stricmp(method, "file") == 0)
		{
			pFileLogger = new FileLogger();
		}
		else if (_stricmp(method, "remote") == 0)
		{
			puts("Remote logging is not supported yet.");
			return false;
		}
		else
		{
			printf("Invalid LogMethod: %s\n", method);
			return false;
		}
	}

	// get dump basename
#ifdef _WIN32
	const char* pLastSlash = strrchr(g_pszConfigIniFile, '\\');
	if (pLastSlash == NULL)
		pLastSlash = strrchr(g_pszConfigIniFile, '/');
#else
	const char* pLastSlash = strrchr(g_pszConfigIniFile, '/');
#endif
	const char* pBaseConfName = (pLastSlash) ? &pLastSlash[1] : g_pszConfigIniFile;
	const char* pDot = strchr(pBaseConfName, '.');
	size_t nBasenameLen;
	if (pDot == NULL)
		nBasenameLen = strlen(pBaseConfName);
	else
		nBasenameLen = pDot - pBaseConfName;

	StringCchCopyA(buff, MAX_PATH, g_pConfFile->GetConfigDir());
	size_t slen = strlen(buff);
	buff[slen] =
#ifdef _WIN32
		'\\';
#else
		'/';
#endif
	StringCchCopyNA(&buff[slen + 1], MAX_PATH - slen, pBaseConfName, nBasenameLen);
	//g_logman.SetDumpInfo(nDumpLevel, buff);

	if (pConsoleLogger != NULL)
	{
		pLogger = pConsoleLogger;
		// TODO: set console log colors
	}
	else if (pFileLogger != NULL)
	{
		pLogger = pFileLogger;

		// set log filenames
		const char* pszFilename = pLogConfig->GetFilename("LogFile");
		if (pszFilename == NULL)
			pszFilename = buff;

		pFileLogger->SetLevelFile(-1, pszFilename);
/*
		const char *pszKey, *pszValue;
		int nKeyCnt = pLogConfig->GetKeyCount();
		for (int k=0; k<nKeyCnt; k++)
		{
		if(!pLogConfig->GetPair(k, &pszKey, &pszValue))
		break;

		if(_strnicmp(pszKey, "File_", 5) == 0)
		{

		}
		}
*/

		int nLogRotateSize = pLogConfig->GetInteger("LogRotateSize");
		if (nLogRotateSize > 0)
			pFileLogger->SetFileRotateSize(nLogRotateSize);
	}

	if (pLogConfig != NULL)
	{
		int nMinLevel = pLogConfig->GetInteger("MinLogLevel", 0);
		if (nMinLevel > 0)
		{
			if (nMinLevel >= 16)
				nMinLevel = 15;

			for (int i = 0; i<nMinLevel; i++)
				pLogger->DisableLevel(i);
		}

		const char* pszDisableLvs = pLogConfig->GetString("DisableLevels");
		if (pszDisableLvs != NULL)
		{
			StringCchCopyA(buff, MAX_PATH, pszDisableLvs);

			const char *delim = ", \t";
			char *tok, *tt;
			tok = strtok_s(buff, delim, &tt);
			while (tok != NULL)
			{
				tok = TrimString(tok);
				if (tok != NULL)
				{
					for (int i = 0;; i++)
					{
						if (s_aLevelNames[i].name == NULL)
							break;

						if (_stricmp(tok, s_aLevelNames[i].name) == 0)
						{
							pLogger->DisableLevel(s_aLevelNames[i].level);
							break;
						}
					}
				}

				tok = strtok_s(NULL, delim, &tt);
			}
		}
	}

	if (!pLogger->Open())
	{
		puts("Log Open failed.");
		delete pLogger;
		return false;
	}

	__svrsideProxy.pLogger = pLogger;

	int nBits;
	const char* pszDebugRelease;

#ifdef SC_64BIT
	nBits = 64;
#else
	nBits = 32;
#endif

#ifdef _DEBUG
	pszDebugRelease = "Debug";
#else
	pszDebugRelease = "Release";
#endif

	Log(LOG_DATE | LOG_SYSTEM, "ServerCore Framework %d-bit %s build (" __TIME__  ", " __DATE__ ")\n", nBits, pszDebugRelease);

	return true;
}

void LogClose()
{
	delete __svrsideProxy.pLogger;
}























void ScAssert(const char* eval, const char* file, int line)
{
	// TODO: logging & call stack dump
//	if(s_loggers[LOG_FATAL] == NULL)
//		EventLog_Error("Assertion failed: %s, file %s, line %d\n", eval, file, line);
//	else
		Log(LOG_FATAL, "Assertion failed: %s, file %s, line %d\n", eval, file, line);

#ifdef _WIN32
	DebugBreak();
#else
    //TODO
#endif
}

SC_NAMESPACE_END
