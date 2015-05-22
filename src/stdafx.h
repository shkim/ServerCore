#pragma once

#ifndef _MSC_VER
//#include "../build/unix/config.h"
#endif

#ifdef _MSC_VER

//////////////////////////////////////////////////////////////////////////////
// Windows platform

#include "targetver.h"

#ifdef SC_PLATFORM_POSIX
#include "pthread.h"
#include "sched.h"
#include "semaphore.h"
#else
#define SC_SERVER_IOCP
#endif

#include <winsock2.h>
#include <windows.h>
#include <intrin.h>
#include <process.h>
#include <mswsock.h>
#include <shlwapi.h>
#include <sqlucode.h>
#include <sql.h>

#include <iostream>
#include <tchar.h>
#include <strsafe.h>
#include <io.h>
#include <stddef.h>

#if _MSC_VER < 1400
// For Visual C++ .NET 2003
#define USER_TIMER_MINIMUM  0x0000000A
#define strtok_s(_a,_b,_c) strtok(_a,_b)
#define _tcstok_s strtok_s
#endif

//////////////////////////////////////////////////////////////////////////////
#else	// _WIN32 (UNIX)

#include "../config.svr/config.h"
#include "../../include/unix_compat.h"
#include "../../samples/helper/unix_strsafe.h"

#ifdef __linux__
#define HAVE_USR_INCLUDE_MALLOC_H
#endif
#ifdef __arm__
#define PTHREAD_MUTEX_RECURSIVE
#endif


#ifdef HAVE_SYS_EPOLL_H
#define SC_SERVER_EPOLL
#elif defined(HAVE_SYS_EVENT_H)
#define SC_SERVER_KQUEUE
#else
#define SC_SERVER_SELECT
#endif
#define SC_PLATFORM_POSIX

#endif	// _WIN32

//////////////////////////////////////////////////////////////////////////////

#if !defined(SC_SERVER_IOCP) && !defined(SC_PLATFORM_POSIX)
#define SC_PLATFORM_POSIX
#endif

#ifndef __arm__
//#define SC_USE_INTELTBB
#endif

#if !defined(_WIN32) && !defined(__linux__)
#define SC_NO_AUTOTLS
#endif

#ifdef SC_USE_INTELTBB
#include "tbb/scalable_allocator.h"
#include "tbb/concurrent_queue.h"
#include "tbb/concurrent_vector.h"
#endif

#include <stdio.h>
#include <string.h>

#include <vector>
#include <queue>
#include <list>
#include <set>
#include <map>
#include <algorithm>

#define SC_NAMESPACE_BEGIN	namespace svrcore {
#define SC_NAMESPACE_END	}

#include "../include/ServerCore.h"
#include "../include/sync.h"
