#include "stdafx.h"
#include "scimpl.h"

SC_NAMESPACE_BEGIN

typedef void* (*ModuleEntryFuncT)(const ServerSideProxyT* p);

static void FreeServerModule(ModuleFileInfoT& mi)
{
	if(mi.pModCtrl && mi.bCreated)
	{
		mi.pModCtrl->Destroy();
		mi.pModCtrl = NULL;
	}

	if(mi.hModule)
	{
		FreeLibrary(mi.hModule);
		mi.hModule = NULL;
	}
}

static bool LoadServerModule(const char* pszModulePath, ModuleFileInfoT* pOut)
{
	int nModVer;//, nVerMajor, nVerMinor;
	ModuleEntryFuncT pfnScModuleEntry;
	char buff[MAX_PATH];

	if(NULL == pszModulePath)
	{
		Log(LOG_FATAL, "ModuleFile value not specified.\n");
		return false;
	}

	if(_access(pszModulePath, 0) < 0)
	{
		StringCchCopy(buff, MAX_PATH, pszModulePath);
		StringCchCat(buff, MAX_PATH,
#ifdef _WIN32
			".dll"
#else
			".so"
#endif
		);

		pszModulePath = buff;
	}

	Log(LOG_SYSTEM, "Loading module %s: ", pszModulePath);

	if(_access(pszModulePath, 0) < 0)
	{
		Log(LOG_FATAL, "FAILED (File not found)\n");
		return false;
	}

#ifdef _WIN32
	pOut->hModule = LoadLibrary(pszModulePath);
	if(pOut->hModule == NULL)
	{
		Log(LOG_FATAL, "FAILED (LoadLibrary err=%d)\n", GetLastError());
#else
	pOut->hModule = dlopen(pszModulePath, RTLD_GLOBAL | RTLD_NOW);
	if(pOut->hModule == NULL)
	{
		Log(LOG_FATAL, "FAILED (dlopen err=%s)\n", dlerror());
#endif
		return false;
	}

	pOut->pModCtrl = NULL;
	pOut->bCreated = false;

	pfnScModuleEntry = (ModuleEntryFuncT) GetProcAddress(pOut->hModule, "ScModuleEntry");
	if(pfnScModuleEntry == NULL)
	{
		Log(LOG_FATAL, "FAILED - Can't find 'ScModuleEntry' in module.\n");
		goto _failed;
	}

	pOut->pModCtrl = (IServerModule*) pfnScModuleEntry(&__svrsideProxy);
	if(pOut->pModCtrl == NULL)
	{
		Log(LOG_FATAL, "FAILED - ScModuleEntry() returned NULL.\n");
		goto _failed;
	}

	nModVer = pOut->pModCtrl->GetVersion();
	if (nModVer != SC_FRAMEWORK_MODULE_VERSION)
	{
		Log(LOG_FATAL, "FAILED - Different spec version(mod=%08x,core=%08x)\n", nModVer, SC_FRAMEWORK_MODULE_VERSION);
		pOut->pModCtrl = NULL;
		goto _failed;
	}

	Log(LOG_SYSTEM, "Success\n");
	return true;

_failed:
	FreeServerModule(*pOut);
	return false;
}

static const char* GetServerModuleFileName(IConfigSection* pSvrSect)
{
	const char* pszModuleFile;

#ifdef _DEBUG
#ifdef SC_64BIT
	pszModuleFile = pSvrSect->GetFilename("DebugModuleFile64");
	if(pszModuleFile == NULL)
		pszModuleFile = pSvrSect->GetFilename("DebugModuleFile");
#else
	pszModuleFile = pSvrSect->GetFilename("DebugModuleFile");
#endif

	if (pszModuleFile != NULL)
		return pszModuleFile;
#endif	// _DEBUG

#ifdef SC_64BIT
	pszModuleFile = pSvrSect->GetFilename("ModuleFile64");
	if(pszModuleFile == NULL)
		pszModuleFile = pSvrSect->GetFilename("ModuleFile");
#else
	pszModuleFile = pSvrSect->GetFilename("ModuleFile");
#endif

#ifdef _DEBUG
	if (pszModuleFile != NULL)
		Log(LOG_WARN, "NOTICE: DebugModuleFile value not found, use (Release) ModuleFile.\n");
#endif

	return pszModuleFile;
}

bool ServerCore::LoadModules(IConfigSection* pSvrSect)
{
	ModuleFileInfoT lmi;
	const char* pszModuleFile = GetServerModuleFileName(pSvrSect);

	if (!LoadServerModule(pszModuleFile, &lmi))
	{
		return false;
	}

	lmi.pPart = NULL;
	m_modules.push_back(lmi);

	for(size_t s=0; s<m_SvrParts.size(); s++)
	{
		ServerPart* pPart = m_SvrParts.at(s);
		pSvrSect = g_core.GetConfigSection(pPart->m_szName);
		if(pSvrSect == NULL)
		{
			// impossible !
			Log(LOG_FATAL, "Config section [%s] not found on module loading.\n", pPart->m_szName);
			return false;
		}

		pszModuleFile = GetServerModuleFileName(pSvrSect);
		if (pszModuleFile != NULL)
		{
			if (!LoadServerModule(pszModuleFile, &lmi))
			{
				return false;
			}

			lmi.pPart = pPart;
			m_modules.push_back(lmi);
		}
	}

	return true;
}


extern const char* g_pszCoreConfigSectionName;
static const char* s_pszDefaultTargetSvr4RegLsnr = NULL;

ServerPart* ServerCore::FindRegTargetSvrPart(const char*& pszTargetSvr)
{
	if(pszTargetSvr == NULL)
		pszTargetSvr = s_pszDefaultTargetSvr4RegLsnr;

	for(size_t i=0; i<m_SvrParts.size(); i++)
	{
		ServerPart* pPart = m_SvrParts.at(i);
		if(_stricmp(pPart->m_szName, pszTargetSvr) == 0)
		{
			return pPart;
		}
	}

	return NULL;
}

void ServerCore::RegisterClientListener(ClientListenerCreatorFuncT pfn, const void* param, const char* pszTargetSvr)
{
	ServerPart* pTargetPart = FindRegTargetSvrPart(pszTargetSvr);

	if(pTargetPart == NULL)
	{
		Log(LOG_FATAL, "RegisterClientHandler(%s): No such named server (IGNORED)\n", pszTargetSvr);
	}
	else if(pTargetPart->m_nListenerTypeID >= 0)
	{
		Log(LOG_FATAL, "RegisterClientHandler(%s): already registerd\n", pszTargetSvr);
		// TODO: exit core?
	}
	else if(pTargetPart->IsTCP() == false)
	{
		Log(LOG_FATAL, "RegisterClientHandler(%s): UDP only\n", pszTargetSvr);
	}
	else
	{
		pTargetPart->m_nListenerTypeID = g_clipool.Register(pfn, param);
	}
}

void ServerCore::RegisterUdpListener(IUdpSocketListener* pListener, const char* pszTargetSvr)
{
	ServerPart* pTargetPart = FindRegTargetSvrPart(pszTargetSvr);

	if(pTargetPart == NULL)
	{
		Log(LOG_FATAL, "RegisterUdpListener(%s): No such named server (IGNORED)\n", pszTargetSvr);
	}
	else if(pTargetPart->m_pUdpSocketListener != NULL)
	{
		Log(LOG_FATAL, "RegisterUdpListener(%s): already registerd\n", pszTargetSvr);
		// TODO: exit core?
	}
	else if(pTargetPart->IsUDP() == false)
	{
		Log(LOG_FATAL, "RegisterUdpListener(%s): TCP only\n", pszTargetSvr);
	}
	else
	{
		g_clipool.RegisterUdp();
		pTargetPart->m_pUdpSocketListener = pListener;
	}
}

IConfigSection* ServerCore::GetConfigSection(const char* pszName)
{
	if(pszName == NULL)
	{
		pszName = (s_pszDefaultTargetSvr4RegLsnr == NULL) ?
			g_pszCoreConfigSectionName : s_pszDefaultTargetSvr4RegLsnr;
	}

	return g_pConfFile->GetSection(pszName);
}

bool ServerCore::InitializeModules()
{
	SC_ASSERT(!m_SvrParts.empty());
	size_t i;

	for(i=0; i<m_modules.size(); i++)
	{
		ModuleFileInfoT& lmi = m_modules.at(i);

		if(lmi.pPart == NULL)
		{
			s_pszDefaultTargetSvr4RegLsnr = m_SvrParts.at(0)->m_szName;
		}
		else
		{
			if(lmi.pPart->m_nListenerTypeID >= 0)
			{
				Log(LOG_FATAL, "ClientListener for [%s] conflicts.\n", lmi.pPart->m_szName);
				return false;
			}

			s_pszDefaultTargetSvr4RegLsnr = lmi.pPart->m_szName;
		}

		lmi.bCreated = true;
		if(!lmi.pModCtrl->Create(this))
			return false;
	}

	// check all modules have a valid starting listener id.
	for(i=0; i<m_SvrParts.size(); i++)
	{
		ServerPart* pPart = m_SvrParts.at(i);

		if(pPart->IsTCP())
		{
			if(pPart->m_nListenerTypeID < 0)
			{
				Log(LOG_FATAL, "ClientListener for [%s] not specified.\n", pPart->m_szName);
				return false;
			}
		}

		if(pPart->IsUDP())
		{
			if(pPart->m_pUdpSocketListener == NULL)
			{
				Log(LOG_FATAL, "UdpListener for [%s] not specified.\n", pPart->m_szName);
				return false;
			}
		}
	}

	s_pszDefaultTargetSvr4RegLsnr = NULL;
	return true;
}

void ServerCore::FreeModules()
{
	for_each(m_modules.begin(), m_modules.end(), FreeServerModule);
}

SC_NAMESPACE_END
