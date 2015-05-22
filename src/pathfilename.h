/*
	PathFileNamer.h

	Path filename utility

	Update History:
	2006-07-03 shkim - Initial import & added copy operator
*/
#ifndef __PATH_FILENAMER_H__
#define __PATH_FILENAMER_H__

#ifndef _WIN32
#include "../../samples/helper/unix_tchar.h"
#include "../../samples/helper/unix_strsafe.h"
#endif

SC_NAMESPACE_BEGIN

class PathFilenamer
{
public:
	PathFilenamer()
	{
		m_szCurDir[0] = 0;
	}

	void operator=(const PathFilenamer& src)
	{
		StringCchCopy(m_szCurDir, MAX_PATH, src.m_szCurDir);
	}

	// returns the basename of the input filename
	//	ex) /abc/def/xyz.ext ==> 'xyz.ext'
	static LPCTSTR GetBaseFilename(LPCTSTR pszPathname)
	{
		const TCHAR* pSlash = _tcsrchr(pszPathname, '/');

		if(pSlash == NULL)
			pSlash = _tcsrchr(pszPathname, '\\');

		if(pSlash == NULL)
			return pszPathname;
		else
			return &pSlash[1];
	}

	static bool IsAbsPath(LPCTSTR pszDir)
	{
#ifdef _WIN32
		return (pszDir[1] == ':' || (pszDir[0] == '\\' && pszDir[1] == '\\'));
#else
		return (pszDir[0] == '/');
#endif
	}

	void ChangeDirectory(LPCTSTR pszDir, bool bIsFilename =false)
	{
		if(IsAbsPath(pszDir))
		{
			// Absolute path: reset current
			StringCchCopy(m_szCurDir, MAX_PATH, pszDir);
		}
		else
		{
			// Relative path: change direcoty
			Append(m_szCurDir, pszDir);
		}

		// if pszDir contains the filename component (at tail), chop it!
		if(bIsFilename)
		{
			TCHAR* pSlash = _tcsrchr(m_szCurDir, '/');
			if(pSlash != NULL)
			{
				*pSlash = 0;
			}
			else
			{
				pSlash = _tcsrchr(m_szCurDir, '\\');
				if(pSlash != NULL)
					*pSlash = 0;
			}
		}
	}

	LPCTSTR GetPathname(LPCTSTR pszFilename)
	{
		StringCchCopy(m_szReturn, MAX_PATH, m_szCurDir);
		Append(m_szReturn, pszFilename);

		return m_szReturn;
	}

	inline LPCTSTR GetCurrentDir()
	{
		return m_szCurDir;
	}
	
	void FromCurrentDirectory()
	{
#ifdef _WIN32
		::GetCurrentDirectory(MAX_PATH, m_szCurDir);
#else
	#ifdef _UNICODE
		char buff[MAX_PATH];
		getcwd(buff, MAX_PATH);
		mbstowcs(m_szCurDir, buff, MAX_PATH);
	#else
		getcwd(m_szCurDir, MAX_PATH);
	#endif
#endif
	}

#ifdef _WIN32
	void FromExeDirectory(HMODULE hModule)
	{
		GetModuleFileName(hModule, m_szCurDir, MAX_PATH);
	}
#endif

private:

#ifdef _WIN32
	static const TCHAR DIR_SEPARATOR = '\\';
#else
	static const TCHAR DIR_SEPARATOR = '/';
#endif

	void Append(LPTSTR pszBase, LPCTSTR pszDir)
	{
		TCHAR* aNameStack[32];

		size_t len = _tcslen(pszBase);
		if(len == 0)
		{
			SC_ASSERT(!"Append(pszBase == '')");
			StringCchCopy(pszBase, MAX_PATH, pszDir);
			return;
		}
		else
		{
			// if pszBase ends with / or \, erase it.
			TCHAR* pLast = &pszBase[len -1];
			if(*pLast == '/' || *pLast == '\\')
				*pLast = 0;
		}

		int nBaseDepth = 0;
		TCHAR* pDst = pszBase;
		while(*pDst != 0)
		{
			if(*pDst == '/' || *pDst == '\\')
			{
				aNameStack[nBaseDepth] = pDst;
				nBaseDepth++;
			}

			pDst++;
		}

		const TCHAR* pSrc = pszDir;
		for(;;)
		{
			if(*pSrc == '/' || *pSrc == '\\' || *pSrc == 0)
			{
				size_t len = pSrc - pszDir;

				if(len == 0)
				{
					// maybe "//" or "\\\\" ==> skip
				}
				else if(len == 1 && *pszDir == '.')
				{
					// skip "."
				}
				else if(len == 2 && (pszDir[0] == '.' && pszDir[1] == '.'))
				{
					// chdir ..
					nBaseDepth--;
					pDst = aNameStack[nBaseDepth];
					*pDst = 0;
				}
				else
				{
					// append
					*pDst = DIR_SEPARATOR;
					aNameStack[nBaseDepth] = pDst;
					nBaseDepth++;

					memcpy(&pDst[1], pszDir, len * sizeof(TCHAR));
					pDst += 1 + len;
					*pDst = 0;
				}

				pszDir = &pSrc[1];
			}

			if(*pSrc == 0)
				break;

			pSrc++;
		}
	}

	TCHAR m_szCurDir[MAX_PATH];
	TCHAR m_szReturn[MAX_PATH];
};

SC_NAMESPACE_END

#endif
