// Common include file for ServerCore samples

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#include <strsafe.h>
#else
#include "../../src/config.svr/config.h"
#include "../../include/unix_compat.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../include/ServerCore.h"
#include "../include/sync.h"
using namespace svrcore;
