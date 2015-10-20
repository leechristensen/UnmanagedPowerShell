// UnmanagedPowerShell.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#pragma region Includes and Imports
#include <windows.h>
#include <comdef.h>
#include <mscoree.h>
#include "PowerShellRunnerDll.h"
#include "UnmanagedPowershell.h"

#include <metahost.h>
#pragma comment(lib, "mscoree.lib")

// Import mscorlib.tlb (Microsoft Common Language Runtime Class Library).
#import "mscorlib.tlb" raw_interfaces_only				\
    high_property_prefixes("_get","_put","_putref")		\
    rename("ReportEvent", "InteropServices_ReportEvent")
using namespace mscorlib;
#pragma endregion

typedef HRESULT(WINAPI *funcCLRCreateInstance)(
	REFCLSID  clsid,
	REFIID     riid,
	LPVOID  * ppInterface
	);

typedef HRESULT (WINAPI *funcCorBindToRuntime)(
	LPCWSTR  pwszVersion,
	LPCWSTR  pwszBuildFlavor,
	REFCLSID rclsid,
	REFIID   riid,
	LPVOID*  ppv);


extern const unsigned int PowerShellRunner_dll_len;
extern unsigned char PowerShellRunner_dll[];
void InvokeMethod(_TypePtr spType, wchar_t* method, wchar_t* command);


bool createDotNetFourHost(HMODULE* hMscoree, const wchar_t* version, ICorRuntimeHost** ppCorRuntimeHost)
{
	HRESULT hr = NULL;
	funcCLRCreateInstance pCLRCreateInstance = NULL;
	ICLRMetaHost *pMetaHost = NULL;
	ICLRRuntimeInfo *pRuntimeInfo = NULL;
	bool hostCreated = false;

	pCLRCreateInstance = (funcCLRCreateInstance)GetProcAddress(*hMscoree, "CLRCreateInstance");
	if (pCLRCreateInstance == NULL)
	{
		wprintf(L"Could not find .NET 4.0 API CLRCreateInstance");
		goto Cleanup;
	}

	hr = pCLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&pMetaHost));
	if (FAILED(hr))
	{
		// For some reason, this still prints on .NET 2.0/3.5 machines...
		wprintf(L"CLRCreateInstance failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	hr = pMetaHost->GetRuntime(L"v2.0.50727", IID_PPV_ARGS(&pRuntimeInfo));
	if (FAILED(hr))
	{
		wprintf(L"ICLRMetaHost::GetRuntime failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	// Check if the specified runtime can be loaded into the process.
	BOOL loadable;
	hr = pRuntimeInfo->IsLoadable(&loadable);
	if (FAILED(hr))
	{
		wprintf(L"ICLRRuntimeInfo::IsLoadable failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	if (!loadable)
	{
		wprintf(L".NET runtime v2.0.50727 cannot be loaded\n");
		goto Cleanup;
	}

	// Load the CLR into the current process and return a runtime interface
	hr = pRuntimeInfo->GetInterface(CLSID_CorRuntimeHost, IID_PPV_ARGS(ppCorRuntimeHost));
	if (FAILED(hr))
	{
		wprintf(L"ICLRRuntimeInfo::GetInterface failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	hostCreated = true;

Cleanup:

	if (pMetaHost)
	{
		pMetaHost->Release();
		pMetaHost = NULL;
	}
	if (pRuntimeInfo)
	{
		pRuntimeInfo->Release();
		pRuntimeInfo = NULL;
	}

	return hostCreated;
}


HRESULT createDotNetTwoHost(HMODULE* hMscoree, const wchar_t* version, ICorRuntimeHost** ppCorRuntimeHost)
{
	HRESULT hr = NULL;
	bool hostCreated = false;
	funcCorBindToRuntime pCorBindToRuntime = NULL;
	
	pCorBindToRuntime = (funcCorBindToRuntime)GetProcAddress(*hMscoree, "CorBindToRuntime");
	if (!pCorBindToRuntime)
	{
		wprintf(L"Could not find API CorBindToRuntime");
		goto Cleanup;
	}

	hr = pCorBindToRuntime(version, L"wks", CLSID_CorRuntimeHost, IID_PPV_ARGS(ppCorRuntimeHost));
	if (FAILED(hr))
	{
		wprintf(L"CorBindToRuntime failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	hostCreated = true;

Cleanup:

	return hostCreated;
}

HRESULT createHost(const wchar_t* version, ICorRuntimeHost** ppCorRuntimeHost)
{
	bool hostCreated = false;

	HMODULE hMscoree = LoadLibrary(L"mscoree.dll");
	
	if (hMscoree)
	{
		if (createDotNetFourHost(&hMscoree, version, ppCorRuntimeHost) || createDotNetTwoHost(&hMscoree, version, ppCorRuntimeHost))
		{
			hostCreated = true;
		}
	}
	
	return hostCreated;
}

int _tmain(int argc, _TCHAR* argv[])
{
	HRESULT hr;

	//ICLRRuntimeHost*        RuntimeClrHost = NULL;


	ICorRuntimeHost *pCorRuntimeHost = NULL;

	IUnknownPtr spAppDomainThunk = NULL;
	_AppDomainPtr spDefaultAppDomain = NULL;


	bool useOldAPIs = false;

	// The .NET assembly to load.
	bstr_t bstrAssemblyName("PowerShellRunner");
	_AssemblyPtr spAssembly = NULL;

	// The .NET class to instantiate.
	bstr_t bstrClassName("PowerShellRunner.PowerShellRunner");
	_TypePtr spType = NULL;



	// Create the runtime host
	if (!createHost(L"v2.0.50727", &pCorRuntimeHost))
	{
		wprintf(L"Failed to create the runtime host\n");
		goto Cleanup;
	}

	
	// Start the CLR
	hr = pCorRuntimeHost->Start();
	if (FAILED(hr))
	{
		wprintf(L"CLR failed to start w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}



	DWORD appDomainId = NULL;
	hr = pCorRuntimeHost->GetDefaultDomain(&spAppDomainThunk);
	if (FAILED(hr))
	{
		wprintf(L"RuntimeClrHost::GetCurrentAppDomainId failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}


	// Get a pointer to the default AppDomain in the CLR.
	hr = pCorRuntimeHost->GetDefaultDomain(&spAppDomainThunk);
	if (FAILED(hr))
	{
		wprintf(L"ICorRuntimeHost::GetDefaultDomain failed w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	hr = spAppDomainThunk->QueryInterface(IID_PPV_ARGS(&spDefaultAppDomain));
	if (FAILED(hr))
	{
		wprintf(L"Failed to get default AppDomain w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	// Load the .NET assembly.
	// (Option 1) Load it from disk - usefully when debugging the PowerShellRunner app (you'll have to copy the DLL into the same directory as the exe)
	hr = spDefaultAppDomain->Load_2(bstrAssemblyName, &spAssembly);
	
	// (Option 2) Load the assembly from memory
	SAFEARRAYBOUND bounds[1];
	bounds[0].cElements = PowerShellRunner_dll_len;
	bounds[0].lLbound = 0;

	SAFEARRAY* arr = SafeArrayCreate(VT_UI1, 1, bounds);
	SafeArrayLock(arr);
	memcpy(arr->pvData, PowerShellRunner_dll, PowerShellRunner_dll_len);
	SafeArrayUnlock(arr);

	hr = spDefaultAppDomain->Load_3(arr, &spAssembly);

	if (FAILED(hr))
	{
		wprintf(L"Failed to load the assembly w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	// Get the Type of PowerShellRunner.
	hr = spAssembly->GetType_2(bstrClassName, &spType);
	if (FAILED(hr))
	{
		wprintf(L"Failed to get the Type interface w/hr 0x%08lx\n", hr);
		goto Cleanup;
	}

	// Call the static method of the class
	wchar_t* argument = L"Get-Process\n\
						 #This is a PowerShell Comment\n\
						 Write-Host \"`n`n*******  The next command is going to throw an exception.  This is planned *********`n`n\"\n\
						 Read-Host\n";
	
	InvokeMethod(spType, L"InvokePS", argument);

Cleanup:

	if (pCorRuntimeHost)
	{
		pCorRuntimeHost->Release();
		pCorRuntimeHost = NULL;
	}

	return 0;
}

void InvokeMethod(_TypePtr spType, wchar_t* method, wchar_t* command)
{
	HRESULT hr;
	bstr_t bstrStaticMethodName(method);
	SAFEARRAY *psaStaticMethodArgs = NULL;
	variant_t vtStringArg(command);
	variant_t vtPSInvokeReturnVal;
	variant_t vtEmpty;


	psaStaticMethodArgs = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	LONG index = 0;
	hr = SafeArrayPutElement(psaStaticMethodArgs, &index, &vtStringArg);
	if (FAILED(hr))
	{
		wprintf(L"SafeArrayPutElement failed w/hr 0x%08lx\n", hr);
		return;
	}

	// Invoke the method from the Type interface.
	hr = spType->InvokeMember_3(
		bstrStaticMethodName, 
		static_cast<BindingFlags>(BindingFlags_InvokeMethod | BindingFlags_Static | BindingFlags_Public), 
		NULL, 
		vtEmpty, 
		psaStaticMethodArgs, 
		&vtPSInvokeReturnVal);

	if (FAILED(hr))
	{
		wprintf(L"Failed to invoke InvokePS w/hr 0x%08lx\n", hr);
		return;
	}
	else
	{
		// Print the output of the command
		wprintf(vtPSInvokeReturnVal.bstrVal);
	}


	SafeArrayDestroy(psaStaticMethodArgs);
	psaStaticMethodArgs = NULL;
}
