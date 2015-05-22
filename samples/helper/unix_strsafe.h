#ifndef __UNIX_STRSAFE_H__
#define __UNIX_STRSAFE_H__

#ifndef _WIN32
#include "unix_w32types.h"
#endif

#define STRSAFE_E_INSUFFICIENT_BUFFER	((HRESULT)0x8007007AL)  // 0x7A = 122L = ERROR_INSUFFICIENT_BUFFER
#define STRSAFE_E_INVALID_PARAMETER		((HRESULT)0x80070057L)  // 0x57 =  87L = ERROR_INVALID_PARAMETER

HRESULT StringCchCopyA
(
	OUT LPSTR  pszDest,
	IN  size_t  cchDest,
	IN  LPCSTR pszSrc
);

HRESULT StringCchCopyNA
(
	OUT LPSTR pszDest,
	IN size_t cchDest,
	IN LPCSTR pszSrc,
	IN size_t cchToCopy
);

HRESULT StringCchCopyW
(
	OUT LPWSTR  pszDest,
	IN  size_t  cchDest,
	IN  LPCWSTR pszSrc
);

HRESULT StringCchCopyNW
(
	OUT LPWSTR pszDest,
	IN size_t cchDest,
	IN LPCWSTR pszSrc,
	IN size_t cchToCopy
);


HRESULT StringCchCatA
(
	OUT LPSTR pszDest,
	IN size_t cchDest,
	IN LPCSTR pszAppend
);

HRESULT StringCchCatNA
(
	OUT LPSTR pszDest,
	IN size_t cchDest,
	IN LPCSTR pszAppend,
	IN size_t cchMaxAppend
);

HRESULT StringCchCatW
(
	OUT LPWSTR pszDest,
	IN size_t cchDest,
	IN LPCWSTR pszAppend
);

HRESULT StringCchCatNW
(
	OUT LPWSTR pszDest,
	IN size_t cchDest,
	IN LPCWSTR pszAppend,
	IN size_t cchMaxAppend
);

HRESULT StringCchPrintfA
(
	OUT LPSTR  pszDest,    
	IN  size_t  cchDest,
	IN  LPCSTR pszFormat,
	...
);

HRESULT StringCchPrintfW
(
	OUT LPWSTR  pszDest,    
	IN  size_t  cchDest,
	IN  LPCWSTR pszFormat,
	...
);

HRESULT StringCchVPrintfA
(
	OUT LPSTR  pszDest,    
	IN  size_t  cchDest,
	IN  LPCSTR pszFormat,
	IN  va_list argList
);

HRESULT StringCchVPrintfW
(
	OUT LPWSTR  pszDest,    
	IN  size_t  cchDest,
	IN  LPCWSTR pszFormat,
	IN  va_list argList
);


#ifdef _UNICODE

#define StringCchCopy		StringCchCopyW
#define StringCchCopyN		StringCchCopyNW
#define StringCchCat		StringCchCatW
#define StringCchCatN		StringCchCatNW
#define StringCchPrintf		StringCchPrintfW
#define StringCchVPrintf	StringCchVPrintfW

#define StringCbCopy		StringCbCopyW
#define StringCbCopyN		StringCbCopyNW
#define StringCbCat			StringCbCatW
#define StringCbCatN		StringCbCatNW
#define StringCbPrintf		StringCbPrintfW
#define StringCbVPrintf		StringCbVPrintfW

#define StringCbCopyW(_d,_c,_s)			StringCchCopyW(_d,(_c)>>1,_s)
#define StringCbCopyNW(_d,_c,_s,_n)		StringCchCopyNW(_d,(_c)>>1,_s,_n)
#define StringCbCatW(_d,_c,_s)			StringCchCatW(_d,(_c)>>1,_s)
#define StringCbCatNW(_d,_c,_s,_n)		StringCchCatNW(_d,(_c)>>1,_s,_n)
#define StringCbPrintfW(_d,_c,_f,...)	StringCchPrintfW(_d,(_c)>>1,_f,__VA_ARGS__)
#define StringCbVPrintfW(_d,_c,_f,_v)	StringCchVPrintfW(_d,(_c)>>1,_f,_v)

#else

#define StringCchCopy		StringCchCopyA
#define StringCchCopyN		StringCchCopyNA
#define StringCchCat		StringCchCatA
#define StringCchCatN		StringCchCatNA
#define StringCchPrintf		StringCchPrintfA
#define StringCchVPrintf	StringCchVPrintfA

#define StringCbCopy		StringCchCopyA
#define StringCbCopyN		StringCchCopyNA
#define StringCbCat			StringCchCatA
#define StringCbCatN		StringCchCatNA
#define StringCbPrintf		StringCchPrintfA
#define StringCbVPrintf		StringCchVPrintfA

#define StringCbCopyA		StringCchCopyA
#define StringCbCopyNA		StringCchCopyNA
#define StringCbCatA		StringCchCatA
#define StringCbCatNA		StringCchCatNA
#define StringCbPrintfA		StringCchPrintfA
#define StringCbVPrintfA	StringCchVPrintfA

#endif

int _wstrlen(const WCHAR* p);

#endif
