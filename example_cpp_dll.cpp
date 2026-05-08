//
//  MODULE:		example_cpp_dll.cpp
//
//  AUTHOR:		Mark Parker
//
//  PURPOSE:	The twin modules: example_cpp_dll.cpp and example_cs_dll.cs show how unmanaged C code
//				can invoke a managed DLL written in C#.
//
//				This file is a mixed-mode C++ module (called a shim) which is linked into
//				legacy unmanaged C code, and enables it to call methods in a managed mode
//				C# module.
//
//				Essentially this is a mixed-mode join between unmanaged code and managed code when
//				the unmanaged is calliing the managed. The context of the managed code is held in
//				MANAGED_LIBRARY_HANDLE which is returned from OpenManagedLibrary().
//
//				THIS DLL SHOULD BE INVOKED BY THE PARENT PROGRAM USING LoadLibraryA() OR EQUIVALENT
//				AND Initialize() CALLED FIRST. THEN, WHEN FUNCTIONS ARE CALLED IN THIS DLL THE MATCHING
//				FUNCTION WILL BE INVOKED IN THE MANAGED DLL.
//
//				The below websites were useful when researching this code:
//
//				http://pragmateek.com/using-c-from-native-c-with-the-help-of-ccli-v2/
//				http://blogs.microsoft.co.il/sasha/2008/02/16/net-to-c-bridge/
//				https://support.microsoft.com/en-gb/kb/311259
//				https://msdn.microsoft.com/en-gb/library/18132394.aspx
//				http://manski.net/2012/06/pinvoke-tutorial-pinning-part-4/
//				https://msdn.microsoft.com/en-us/library/75dwhxf7%28v=vs.110%29.aspx?f=255&MSPPError=-2147217396
//				https://msdn.microsoft.com/en-us/library/481fa11f.aspx?f=255&MSPPError=-2147217396
//				http://stackoverflow.com/questions/7371775/how-can-i-send-a-managed-object-to-native-function-to-use-it
//				http://stackoverflow.com/questions/1373100/how-to-add-folder-to-assembly-search-path-at-runtime-in-net
//

//	Platform includes
#include "win.h"
#include "Shlwapi.h"

//	Language includes
#include "vcclr.h"
#include "stdio.h"

//	Namespaces
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Reflection;
using namespace Bridge;									// defined in example_cs_dll.cs

//	types
typedef void*	MANAGED_LIBRARY_HANDLE;

//	Defines
#define INVALID_MANAGED_LIBRARY_HANDLE	((MANAGED_LIBRARY_HANDLE) -1)

//
//	FUNCTION:	WriteError
//
//	PURPOSE:	This module has no real error reporting capability. It cannot access
//				either of managed or unmanaged logging so it just logs errors
//				by appending lines to a fixed file name in the current working directory.
//
#pragma unmanaged
void WriteError(char *sFormat, ...)
{
	FILE		*fp = 0;
	char        sBuffer[4096];
	char		sPreamble[256];
	va_list     ap;

	//	Resolve all args
	va_start(ap, sFormat);
	vsprintf_s(sBuffer, sizeof(sBuffer), sFormat, ap);
	va_end(ap);

	//	Compose a preamble
	char		sDate[128];
	char		sTime[128];
	SYSTEMTIME	SystemTime;

	GetLocalTime(&SystemTime);

	GetDateFormatA(LOCALE_SYSTEM_DEFAULT, 0, &SystemTime, "dd'-'MMM'-'yyyy", sDate, sizeof(sDate));
	GetTimeFormatA(LOCALE_SYSTEM_DEFAULT, 0, &SystemTime, "HH':'mm':'ss", sTime, sizeof(sTime));

	sprintf_s(sPreamble, sizeof(sPreamble), "%s %s:%03d", sDate, sTime, (int)SystemTime.wMilliseconds);

	//	Open/append the output file
	fopen_s(&fp, "MIXED_MODE_ERROR.err", "a");

	//	Write the line
	if (fp != 0)
	{
		fprintf(fp, "%s %s", sPreamble, sBuffer);

		//	New line if required
		if (sBuffer[0] != '\0' && (sBuffer[strlen(sBuffer) - 1] != '\n'))
			fprintf(fp, "\n");

		//	Close the file
		fclose(fp);
	}

	return;
}

//
//	FUNCTION:	sprintf_trunc
//
//	PURPOSE:	This is a safe "truncating" version of sprintf_s. If the output buffer is too
//				small, rather than crashing the app, it does what it can.
//
void sprintf_trunc(char *sBuffer, size_t sizBuffer, char *sFormat, ...)
{
	//	Check we have a buffer
	if (sBuffer == 0)
		return;

	//	Check we have at least one data byte available in the buffer
	switch (sizBuffer)
	{
	case 0:

		return;
	
	case 1:

		sBuffer[0] = '\0';
		return;

	default:

		//	That's OK
		break;
	}

	//	Fill buffer with nulls
	memset(sBuffer, 0, sizBuffer);

	//	Check we have a format specifier
	if (sFormat == 0)
	{
		char *err = "error: format missing in sprintf_trunc";
		for (size_t i = 0; i < sizBuffer - 1; i++)
			sBuffer[i] = err[i];
		return;
	}

	//	Compute output buffer size requirement
	va_list ap;
	va_start(ap, sFormat);
	size_t iLenRequired = (size_t) _vscprintf(sFormat, ap);
	va_end(ap);

	//	We will impose an arbitrary size limit to avoid ridiculously 
	//	long strings.
	if (iLenRequired > 10000)
	{
		char *err = "error: formatted string too long in sprintf_trunc";
		for (size_t i = 0; i < sizBuffer - 1; i++)
			sBuffer[i] = err[i];
		return;
	}

	//	Allocate a sufficient buffer to write the string to
	char *sFormatted = new char[iLenRequired + 32];
	
	//	Format the string into the output buffer
	va_start (ap, sFormat);
	vsprintf_s (sFormatted, (size_t) iLenRequired + 16, sFormat, ap);                                        
	va_end (ap);

	//	Copy the formatted string to the output buffer as far as it goes,
	//	preserving the existing last null
	for (size_t i = 0; (sFormatted[i] != '\0') && (i < sizBuffer - 1); i++)
		sBuffer[i] = sFormatted[i];

	//	Flag truncation if it happened
	if (iLenRequired > (sizBuffer - 1))
		sBuffer[sizBuffer - 2] = '|';

	//	Free the memory we allocated
	delete [] sFormatted;

	//	That's all folks!
	return;
}

#pragma managed
//
//	FUNCTION:	LocalLoader
//
//	PURPOSE:	This routine resolves and loads .NET assemblies which may not
//				be where Windows would expect them.
//
//				This example only looks in the current working directory, but more
//				places could be added.
//
Assembly^ LocalLoader(System::Object ^sender, System::ResolveEventArgs^ args)
{
	Assembly^ assRet = nullptr;
	String ^strAssemblyName = nullptr;
	char *sName = 0;
	char sReportName[256];
	char sFullName[256];
	char sFullPath[1024];

	strcpy_s(sReportName, sizeof(sReportName), "*NOT SET*");

	try
	{
		strAssemblyName = (gcnew AssemblyName(args->Name))->Name;
		sName = (char*)(void*)Marshal::StringToHGlobalAnsi(strAssemblyName);
		strcpy_s(sReportName, sizeof(sReportName), sName);
		sprintf_s(sFullName, "%s.dll", sName);

		//	Try CWD
		if (assRet == nullptr)
		{
			if (PathFileExistsA(sFullName))
			{
				String^ strFullPath = gcnew String(sFullName);
				assRet = Assembly::LoadFrom(strFullPath);
			}
		}

		//	Try elsewhere
		if (assRet == nullptr)
		{
			//	...
		}

		//	Etc

		Marshal::FreeHGlobal((System::IntPtr) sName);
	}
	catch (exception e)
	{
		WriteError("example_cpp_dll.cpp/LocalLoader() failed: %s\n", e.what());
	}

	//WriteError("example_cpp_dll.cpp/LocalLoader() wanted %s, loaded %s\n", sReportName, assRet != nullptr ? sFullPath : "nothing");

	return assRet;
}

//
//	FUNCTION:	Initialize  (** CALL ME FIRST!! **)
//
//	PURPOSE:	Hooks into the .NET assembly loader so we can force
//				the loading of our own modules.
//
//				P1		Address of an ASCII error buffer
//				P2		Size in bytes of the error buffer
//
//				Ret		TRUE - no error
//						FALSE - error, see error buffer for details
//
extern "C" __declspec(dllexport) BOOL __cdecl Initialize(char *sError, size_t sizError)
{
	bool ok = true;
	static bool bHooked = false;

	//	Init
	if (ok)
	{
		sprintf_trunc(sError, sizError, "");
	}

	//	Report
	if (ok)
	{
		//WriteError("Initialise() called...\n");
	}

	//	Apply hook
	if (ok)
	{
		if (!bHooked) try
		{
			AppDomain::CurrentDomain->AssemblyResolve += gcnew ResolveEventHandler(&LocalLoader);
			bHooked = true;
		}
		catch (exception e)
		{
			ok = false;
			sprintf_trunc(sError, sizError, "example_cpp_dll.dll/Initialize(): %s", e.what());
		}
	}

	return ok ? TRUE : FALSE;
}

//	
//	FUNCTION:	OpenManagedLibrary
//
//	PURPOSE:	This is a shim function whose main purpose is to
//				pass control to the C# function of the same name.
//
//				Params: Address and size in bytes of an error buffer
//
extern "C" MANAGED_LIBRARY_HANDLE __declspec(dllexport) __cdecl OpenManagedLibrary(
	char *sMessage,
	size_t sizMessage)
{
	//	Allocate a managed library object on the unmanaged heap
	gcroot<ManagedLibrary^> managedLibrary = gcnew ManagedLibrary();

	//	Pin the message
	pin_ptr<unsigned char> pMessage = (unsigned char *)sMessage;

	//	Call the open managed library method
	BOOL iok = FALSE;
	try
	{
		 iok = managedLibrary->OpenManagedLibrary(pMessage, (int)sizMessage);
	}
	catch (exception e)
	{
		iok = FALSE;
		WriteError("OpenManagedLibrary: exception: %s\n", e.what());
	}

	//	Return the 'address' of the managed library object or INVALID_MANAGED_LIBRARY_HANDLE on error
	return (iok != 0) ? (MANAGED_LIBRARY_HANDLE) GCHandle::ToIntPtr(GCHandle::Alloc(managedLibrary)).ToPointer() : INVALID_MANAGED_LIBRARY_HANDLE;
}

//	
//	FUNCTION:	CloseManagedLibrary
//
//	PURPOSE:	This is a shim function whose main purpose is to
//				pass control to the C# function of the same name.
//
extern "C" void __declspec(dllexport) CloseManagedLibrary(MANAGED_LIBRARY_HANDLE hManagedLibrary)
{
	try
	{
		//	Convert the handle to managed object address
		IntPtr ip(hManagedLibrary);
		ManagedLibrary^ managedLibrary = (ManagedLibrary^)GCHandle::FromIntPtr(ip).Target;

		//	Make the call
		managedLibrary->CloseManagedLibrary();

		//	Dispose of the managed library
		delete managedLibrary;

		//	Dispose of the handle
		GCHandle::FromIntPtr(ip).Free();
	}
	catch (exception e)
	{
		WriteError("CloseManagedLibrary: exception: %s\n", e.what());
	}

	//	All done
	return;
}

//	
//	FUNCTION:	ReadData
//
//	PURPOSE:	This is an example of how to read a data buffer from the managed library
//
//				P1		The managed library handle returned by OpenManagedLibrary()
//				P2		Address where read data should be stored in unmanaged code
//				P3		Size in bytes of the P2 buffer
//				P4		Address of a size_t cell where the number of bytes returned is written
//
//				Returns	TRUE on success
//						FALSE on error
//				
//				An example GetError() function is shown futher down
//
extern "C" BOOL __declspec(dllexport) ReadData(MANAGED_LIBRARY_HANDLE hManagedLibrary, void *pData, size_t sizData, size_t *psizBytesRead)
{
	BOOL iok = TRUE;

	try
	{
		//	Convert the handle to managed object address
		IntPtr ip(hManagedHandle);
		ManagedHandle^ managedHandle = (ManagedHandle^)GCHandle::FromIntPtr(ip).Target;

		//	Local copies of correct type
		int iDataIn = (int)sizData;
		int iDataOut = 0;

		//	Fix buffer in memory
		pin_ptr<int> pDataOut = &iDataOut;

		//	Call the managed rtn
		iok = managedHandle->ReadData((unsigned char *)pData, iDataIn, pDataOut);

		*psizBytesRead = (size_t)iDataOut;
	}
	catch (exception e)
	{
		iok = FALSE;
		WriteError("ReadData: exception: %s\n", e.what());
	}

	//	All done
	return iok;
}

//	
//	FUNCTION:	WriteData
//
//	PURPOSE:	This is an example of how to write data to the managed library.
//
//				P1		The managed library handle returned by OpenManagedLibrary()
//				P2		Address where data to be writer is stored in unmanaged code
//				P3		Number of bytes to write
//
//				Returns	TRUE on success
//						FALSE on error
//				
//
extern "C" BOOL __declspec(dllexport) WriteData(MANAGED_LIBRARY_HANDLE hManagedLibrary, const void *pData, size_t sizData)
{
	BOOL iok = TRUE;

	try
	{
		//	Convert the handle to managed object address
		IntPtr ip(hManagedLibrary);
		ManagedLibrary^ managedLibrary = (ManagedLibrary^)GCHandle::FromIntPtr(ip).Target;

		//	Local copy of correct type
		int iSizData = (int)sizData;

		//	Get a pinned pointer. (That said, pData is either stack or native heap so
		//	this is probably not necessary.)
		pin_ptr<unsigned char> pMyData = (unsigned char*)pData;

		//	Call the managed rtn
		iok = managedLibrary->WriteData(pMyData, iSizData);
	}
	catch (exception e)
	{
		iok = FALSE;
		WriteError("WriteData: exception: %s\n", e.what());
	}

	//	All done
	return iok;
}


//	
//	FUNCTION:	GetError
//
//	PURPOSE:	This rtn shows how to get the most recent error from the managed library
//				
//				P1		Handle of managed library
//				P2		Address of error buffer
//				P3		Size in bytes of error buffer
//
extern "C" void __declspec(dllexport) GetError(MANAGED_LIBRARY_HANDLE hManagedLibrary, char *sError, size_t sizError)
{
	try
	{
		//	Convert the handle to managed object address
		IntPtr ip(hManagedLibrary);
		ManagedLibrary^ managedLibrary = (managedLibrary^)GCHandle::FromIntPtr(ip).Target;

		//	Get error and convert to char*
		char *ptr = (char*)(void*)Marshal::StringToHGlobalAnsi(managedLibrary->GetError());
		sprintf_trunc(sError, sizError, "%s", ptr);
		Marshal::FreeHGlobal((System::IntPtr) ptr);
	}
	catch (exception e)
	{
		sprintf_trunc(sError, sizError, "shim exception: %s", e.what());
		WriteError("GetError: exception: %s\n", e.what());
	}

	//	All done
	return;
}
