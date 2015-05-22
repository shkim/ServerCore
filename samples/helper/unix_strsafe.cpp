/*
	unix_strsafe.h - emulates some core functions of <strsafe.h> in MS Windows SDK
*/
#include "stdafx.h"
#include "unix_strsafe.h"

HRESULT StringCchCopyA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszSrc)
{
	if(!pszDest || !pszSrc)
		return STRSAFE_E_INVALID_PARAMETER;

	for(size_t i=0; i<cchDest; i++)
	{
		if((pszDest[i] = pszSrc[i]) == 0)
			return S_OK;
	}

	pszDest[cchDest -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCopyW(OUT LPWSTR pszDest, IN size_t cchDest, IN LPCWSTR pszSrc)
{
	if(!pszDest || !pszSrc)
		return STRSAFE_E_INVALID_PARAMETER;

	for(size_t i=0; i<cchDest; i++)
	{
		if((pszDest[i] = pszSrc[i]) == 0)
			return S_OK;
	}

	pszDest[cchDest -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCopyNA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszSrc, IN size_t cchSrc)
{
	if(!pszDest || !pszSrc)
		return STRSAFE_E_INVALID_PARAMETER;

	++cchSrc;
	size_t cchMin = cchDest < cchSrc ? cchDest : cchSrc;
	for(size_t i=0; i<cchMin; i++)
	{
		if((pszDest[i] = pszSrc[i]) == 0)
			return S_OK;
	}

	pszDest[cchMin -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCopyNW(OUT LPWSTR pszDest, IN size_t cchDest, IN LPCWSTR pszSrc, IN size_t cchSrc)
{
	if(!pszDest || !pszSrc)
		return STRSAFE_E_INVALID_PARAMETER;

	++cchSrc;
	size_t cchMin = cchDest < cchSrc ? cchDest : cchSrc;
	for(size_t i=0; i<cchMin; i++)
	{
		if((pszDest[i] = pszSrc[i]) == 0)
			return S_OK;
	}

	pszDest[cchMin -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

//////////////////////////////////////////////////////////////////////////////

HRESULT StringCchCatA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszAppend)
{
	if(!pszDest || !pszAppend)
		return STRSAFE_E_INVALID_PARAMETER;

	while(*pszDest != 0)
	{
		if(--cchDest <= 0)
			return STRSAFE_E_INSUFFICIENT_BUFFER;

		pszDest++;
	}

	for(size_t i=0; i<cchDest; i++)
	{
		if((pszDest[i] = pszAppend[i]) == 0)
			return S_OK;
	}

	pszDest[cchDest -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCatW(OUT LPWSTR pszDest, IN size_t cchDest, IN LPCWSTR pszAppend)
{
	if(!pszDest || !pszAppend)
		return STRSAFE_E_INVALID_PARAMETER;

	while(*pszDest != 0)
	{
		if(--cchDest <= 0)
			return STRSAFE_E_INSUFFICIENT_BUFFER;

		pszDest++;
	}

	for(size_t i=0; i<cchDest; i++)
	{
		if((pszDest[i] = pszAppend[i]) == 0)
			return S_OK;
	}

	pszDest[cchDest -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCatNA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszAppend, IN size_t cchMaxAppend)
{
	if(!pszDest || !pszAppend)
		return STRSAFE_E_INVALID_PARAMETER;

	while(*pszDest != 0)
	{
		if(--cchDest <= 0)
			return STRSAFE_E_INSUFFICIENT_BUFFER;

		pszDest++;
	}

	++cchMaxAppend;
	size_t cchMin = cchDest < cchMaxAppend ? cchDest : cchMaxAppend;
	for(size_t i=0; i<cchMin; i++)
	{
		if((pszDest[i] = pszAppend[i]) == 0)
			return S_OK;
	}

	pszDest[cchMin -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchCatNW(OUT LPWSTR pszDest, IN size_t cchDest, IN LPCWSTR pszAppend, IN size_t cchMaxAppend)
{
	if(!pszDest || !pszAppend)
		return STRSAFE_E_INVALID_PARAMETER;

	while(*pszDest != 0)
	{
		if(--cchDest <= 0)
			return STRSAFE_E_INSUFFICIENT_BUFFER;

		pszDest++;
	}

	++cchMaxAppend;
	size_t cchMin = cchDest < cchMaxAppend ? cchDest : cchMaxAppend;
	for(size_t i=0; i<cchMin; i++)
	{
		if((pszDest[i] = pszAppend[i]) == 0)
			return S_OK;
	}

	pszDest[cchMin -1] = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

//////////////////////////////////////////////////////////////////////////////

HRESULT StringCchPrintfA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszFormat, ...)
{
	if(!pszDest || !pszFormat)
		return STRSAFE_E_INVALID_PARAMETER;

	va_list ap;
	va_start(ap, pszFormat);

	vsnprintf(pszDest, cchDest, pszFormat, ap);
	pszDest[cchDest -1] = 0;

	va_end(ap);
	return S_OK;
}

HRESULT StringCchVPrintfA(OUT LPSTR pszDest, IN size_t cchDest, IN LPCSTR pszFormat, IN va_list argList)
{
	if(!pszDest || !pszFormat)
		return STRSAFE_E_INVALID_PARAMETER;

	vsnprintf(pszDest, cchDest, pszFormat, argList);
	pszDest[cchDest -1] = 0;

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////////
// quick and dirty sprintf implementation
// (TODO: floating point, more tests)

#define TEMP_NUMBER_BUFFER_SIZE		32

#define FLAG_SIGNED_NUM		0x01
#define FLAG_CAPITAL		0x02
#define FLAG_PLUS			0x04
#define FLAG_LEFT_ALIGN		0x08
#define FLAG_ZEROPAD		0x10

static inline bool isDigit(WCHAR ch)
{
	return (ch >= '0' && ch <= '9');
}

static int readDecimal(const WCHAR*& p)
{
	int num = 0;

	while(isDigit(*p))
		num = num*10 + (*p++ - '0');

	return num;
}

static WCHAR* makeNumberStr(WCHAR *dst, INT64 num, int base, int flags, WCHAR* pSign)
{
	WCHAR sign = 0;
	const WCHAR *digits = (flags & FLAG_CAPITAL)
		? (WCHAR*) L"0123456789ABCDEF"
		: (WCHAR*) L"0123456789abcdef";

	dst += TEMP_NUMBER_BUFFER_SIZE -1;
	*dst = 0;

	if(flags & FLAG_SIGNED_NUM)
	{
		if(num < 0)
		{
			sign = '-';
			num = -num;
		}
		else if(flags & FLAG_PLUS)
		{
			sign = '+';
		}
	}

	do
	{
		int remainder = num % base;
		num /= base;
		*--dst = digits[remainder];
	}
	while(num > 0);

	if(sign != 0 && (flags & (FLAG_LEFT_ALIGN|FLAG_ZEROPAD)) == 0)
	{
		*--dst = sign;
		*pSign = 0;
	}
	else
	{
		*pSign = sign;
	}

	return dst;
}

int _wstrlen(const WCHAR* p)
{
	const WCHAR* p0 = p;
	while(*p != 0) p++;
	return (int)(p - p0);
}

HRESULT StringCchVPrintfW(OUT LPWSTR pszDest, IN size_t _cchDest, IN LPCWSTR pszFormat, IN va_list argList)
{
	int width, slen, cchDest = (int) _cchDest;
	UINT flags, base;
	UINT64 num;
	WCHAR sign, buff[TEMP_NUMBER_BUFFER_SIZE];
	WCHAR* pStrArg;

	if(!pszDest || !pszFormat)
		return STRSAFE_E_INVALID_PARAMETER;

	for(; cchDest > 1; ++pszFormat)
	{
		WCHAR ch = *pszFormat;
		if(ch != '%')
		{
			if((*pszDest++ = ch) == 0)
				return S_OK;

			--cchDest;
			continue;
		}

		flags = 0;

_readMoreFlag:
		ch = *++pszFormat;
		switch(ch)
		{
		case '-':
			flags |= FLAG_LEFT_ALIGN;
			goto _readMoreFlag;

		case '+':
			flags |= FLAG_PLUS;
			goto _readMoreFlag;

		case '0':
			flags |= FLAG_ZEROPAD;
			goto _readMoreFlag;
		}

		if(isDigit(ch))
		{
			width = readDecimal(pszFormat);
			ch = *pszFormat;
		}
		else
		{
			width = 0;
		}

		switch(ch)
		{
		case 'c':
			if(width > 1 && !(flags & FLAG_LEFT_ALIGN))
			{
				if(width < cchDest)
				{
					cchDest -= width;
					while(--width > 0)
						*pszDest++ = ' ';
				}
				else
				{
					while(--cchDest > 1)
					{
						*pszDest++ = ' ';
					}

					*pszDest = 0;
					return STRSAFE_E_INSUFFICIENT_BUFFER;
				}
			}

			*pszDest++ = va_arg(argList, int);
			--cchDest;

			if(--width > 0)
				goto _padRight;
			continue;

		case 's':
			pStrArg = va_arg(argList, WCHAR*);
			if(pStrArg == NULL)
				pStrArg = (WCHAR*) L"(null)";

			if(!(flags & FLAG_LEFT_ALIGN))
			{
				slen = _wstrlen(pStrArg);
				if(slen < width)
				{
					width -= slen;

					if(width < cchDest)
					{
						cchDest -= width;
						while(--width >= 0)
							*pszDest++ = ' ';
					}
					else
					{
						while(--cchDest > 1)
						{
							*pszDest++ = ' ';
						}

						*pszDest = 0;
						return STRSAFE_E_INSUFFICIENT_BUFFER;
					}
				}
			}

			for(; cchDest > 1; --cchDest, ++pszDest, --width)
			{
				if((*pszDest = *pStrArg++) == 0)
					break;
			}

			goto _padRight;

		case 'p':
			flags |= FLAG_CAPITAL | FLAG_ZEROPAD;
			base = 16;
#ifdef SC_64BIT
			width = 16;
#else
			width = 8;
#endif
			break;

		case 'X':
			flags |= FLAG_CAPITAL;
		case 'x':
			base = 16;
			break;

		case 'd':
		case 'i':
			flags |= FLAG_SIGNED_NUM;
		case 'u':
			base = 10;
			break;

		default:
			if(ch != '%')
			{
				*pszDest++ = '%';
				cchDest--;
			}

			if(ch == 0)
			{
				pszFormat--;
			}
			else
			{
				*pszDest++ = ch;
				cchDest--;
			}

			continue;
		}

		if (flags & FLAG_SIGNED_NUM)
			num = va_arg(argList, int);
		else
			num = va_arg(argList, unsigned int);

		pStrArg = makeNumberStr(buff, num, base, flags, &sign);

		if(sign != 0)
		{
			*pszDest++ = sign;
			--cchDest;
			--width;
		}

		slen = (int)(&buff[TEMP_NUMBER_BUFFER_SIZE -1] - pStrArg);

		if((flags & FLAG_LEFT_ALIGN) || width <= slen)
		{
			for(; cchDest > 1; --cchDest, ++pszDest, --width)
			{
				if((*pszDest = *pStrArg++) == 0)
					break;
			}

			goto _padRight;
		}
		else
		{
			sign = (flags & FLAG_ZEROPAD) ? '0' : ' ';
			width -= slen;

			if(width < cchDest)
			{
				cchDest -= width;
				while(--width >= 0)
					*pszDest++ = sign;
			}
			else
			{
				while(--cchDest > 1)
				{
					*pszDest++ = sign;
				}

				*pszDest = 0;
				return STRSAFE_E_INSUFFICIENT_BUFFER;
			}

			for(; cchDest > 1; --cchDest, ++pszDest)
			{
				if((*pszDest = *pStrArg++) == 0)
					break;
			}

			continue;
		}

_padRight:
		if(width > 0)
		{
			if(width < cchDest)
			{
				cchDest -= width;
				while(--width >= 0)
					*pszDest++ = ' ';
			}
			else
			{
				while(--cchDest > 1)
				{
					*pszDest++ = ' ';
				}

				*pszDest = 0;
				return STRSAFE_E_INSUFFICIENT_BUFFER;
			}
		}
	}

	*pszDest = 0;
	return STRSAFE_E_INSUFFICIENT_BUFFER;
}

HRESULT StringCchPrintfW(OUT LPWSTR pszDest, IN size_t cchDest, IN LPCWSTR pszFormat, ...)
{
	va_list ap;
	va_start(ap, pszFormat);
	
	StringCchVPrintfW(pszDest, cchDest, pszFormat, ap);
	
	va_end(ap);
	return S_OK;
}
