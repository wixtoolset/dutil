// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

#include "precomp.h"


// Exit macros
#define ResrExitOnLastError(x, s, ...) ExitOnLastErrorSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitOnLastErrorDebugTrace(x, s, ...) ExitOnLastErrorDebugTraceSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitWithLastError(x, s, ...) ExitWithLastErrorSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitOnFailure(x, s, ...) ExitOnFailureSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitOnRootFailure(x, s, ...) ExitOnRootFailureSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitOnFailureDebugTrace(x, s, ...) ExitOnFailureDebugTraceSource(DUTIL_SOURCE_RESRUTIL, x, s, __VA_ARGS__)
#define ResrExitOnNull(p, x, e, s, ...) ExitOnNullSource(DUTIL_SOURCE_RESRUTIL, p, x, e, s, __VA_ARGS__)
#define ResrExitOnNullWithLastError(p, x, s, ...) ExitOnNullWithLastErrorSource(DUTIL_SOURCE_RESRUTIL, p, x, s, __VA_ARGS__)
#define ResrExitOnNullDebugTrace(p, x, e, s, ...)  ExitOnNullDebugTraceSource(DUTIL_SOURCE_RESRUTIL, p, x, e, s, __VA_ARGS__)
#define ResrExitOnInvalidHandleWithLastError(p, x, s, ...) ExitOnInvalidHandleWithLastErrorSource(DUTIL_SOURCE_RESRUTIL, p, x, s, __VA_ARGS__)
#define ResrExitOnWin32Error(e, x, s, ...) ExitOnWin32ErrorSource(DUTIL_SOURCE_RESRUTIL, e, x, s, __VA_ARGS__)
#define ResrExitOnGdipFailure(g, x, s, ...) ExitOnGdipFailureSource(DUTIL_SOURCE_RESRUTIL, g, x, s, __VA_ARGS__)

#define RES_STRINGS_PER_BLOCK 16


BOOL CALLBACK EnumLangIdProc(
    __in_opt HMODULE hModule,
    __in_z LPCSTR lpType,
    __in_z LPCSTR lpName,
    __in WORD wLanguage,
    __in LONG_PTR lParam
    );

/********************************************************************
ResGetStringLangId - get the language id for a string in the string table.

********************************************************************/
extern "C" HRESULT DAPI ResGetStringLangId(
    __in_opt LPCWSTR wzPath,
    __in UINT uID,
    __out WORD *pwLangId
    )
{
    Assert(pwLangId);

    HRESULT hr = S_OK;
    HINSTANCE hModule = NULL;
    DWORD dwBlockId = (uID / RES_STRINGS_PER_BLOCK) + 1;
    WORD wFoundLangId = 0;

    if (wzPath && *wzPath)
    {
        hModule = LoadLibraryExW(wzPath, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);
        ResrExitOnNullWithLastError(hModule, hr, "Failed to open resource file: %ls", wzPath);
    }

#pragma prefast(push)
#pragma prefast(disable:25068)
    if (!::EnumResourceLanguagesA(hModule, RT_STRING, MAKEINTRESOURCE(dwBlockId), static_cast<ENUMRESLANGPROC>(EnumLangIdProc), reinterpret_cast<LONG_PTR>(&wFoundLangId)))
#pragma prefast(pop)
    {
        ResrExitWithLastError(hr, "Failed to find string language identifier.");
    }

    *pwLangId = wFoundLangId;

LExit:
    if (hModule)
    {
        ::FreeLibrary(hModule);
    }

    return hr;
}


/********************************************************************
ResReadString

NOTE: ppwzString should be freed with StrFree()
********************************************************************/
extern "C" HRESULT DAPI ResReadString(
    __in HINSTANCE hinst,
    __in UINT uID,
    __deref_out_z LPWSTR* ppwzString
    )
{
    Assert(hinst && ppwzString);

    HRESULT hr = S_OK;
    DWORD cch = 64;  // first guess
    DWORD cchReturned = 0;

    do
    {
        hr = StrAlloc(ppwzString, cch);
        ResrExitOnFailureDebugTrace(hr, "Failed to allocate string for resource id: %d", uID);

        cchReturned = ::LoadStringW(hinst, uID, *ppwzString, cch);
        if (0 == cchReturned)
        {
            ResrExitWithLastError(hr, "Failed to load string resource id: %d", uID);
        }

        // if the returned string count is one character too small, it's likely we have
        // more data to read
        if (cchReturned + 1 == cch)
        {
            cch *= 2;
            hr = S_FALSE;
        }
    } while (S_FALSE == hr);
    ResrExitOnFailure(hr, "Failed to load string resource id: %d", uID);

LExit:
    return hr;
}


/********************************************************************
 ResReadStringAnsi

 NOTE: ppszString should be freed with StrFree()
********************************************************************/
extern "C" HRESULT DAPI ResReadStringAnsi(
    __in HINSTANCE hinst,
    __in UINT uID,
    __deref_out_z LPSTR* ppszString
    )
{
    Assert(hinst && ppszString);

    HRESULT hr = S_OK;
    DWORD cch = 64;  // first guess
    DWORD cchReturned = 0;

    do
    {
        hr = StrAnsiAlloc(ppszString, cch);
        ResrExitOnFailureDebugTrace(hr, "Failed to allocate string for resource id: %d", uID);

#pragma prefast(push)
#pragma prefast(disable:25068)
        cchReturned = ::LoadStringA(hinst, uID, *ppszString, cch);
#pragma prefast(pop)
        if (0 == cchReturned)
        {
            ResrExitWithLastError(hr, "Failed to load string resource id: %d", uID);
        }

        // if the returned string count is one character too small, it's likely we have
        // more data to read
        if (cchReturned + 1 == cch)
        {
            cch *= 2;
            hr = S_FALSE;
        }
    } while (S_FALSE == hr);
    ResrExitOnFailure(hr, "failed to load string resource id: %d", uID);

LExit:
    return hr;
}


/********************************************************************
ResReadData - returns a pointer to the specified resource data

NOTE:  there is no "free" function for this call
********************************************************************/
extern "C" HRESULT DAPI ResReadData(
    __in_opt HINSTANCE hinst,
    __in_z LPCSTR szDataName,
    __deref_out_bcount(*pcb) PVOID *ppv,
    __out DWORD *pcb
    )
{
    Assert(szDataName);
    Assert(ppv);

    HRESULT hr = S_OK;
    HRSRC hRsrc = NULL;
    HGLOBAL hData = NULL;
    DWORD cbData = 0;

#pragma prefast(push)
#pragma prefast(disable:25068)
    hRsrc = ::FindResourceExA(hinst, RT_RCDATA, szDataName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
#pragma prefast(pop)
    ResrExitOnNullWithLastError(hRsrc, hr, "Failed to find resource.");

    hData = ::LoadResource(hinst, hRsrc);
    ResrExitOnNullWithLastError(hData, hr, "Failed to load resource.");

    cbData = ::SizeofResource(hinst, hRsrc);
    if (!cbData)
    {
        ResrExitWithLastError(hr, "Failed to get size of resource.");
    }

    *ppv = ::LockResource(hData);
    ResrExitOnNullWithLastError(*ppv, hr, "Failed to lock data resource.");
    *pcb = cbData;

LExit:
    return hr;
}


/********************************************************************
ResExportDataToFile - extracts the resource data to the specified target file

********************************************************************/
extern "C" HRESULT DAPI ResExportDataToFile(
    __in_z LPCSTR szDataName,
    __in_z LPCWSTR wzTargetFile,
    __in DWORD dwCreationDisposition
    )
{
    HRESULT hr = S_OK;
    PVOID pData = NULL;
    DWORD cbData = 0;
    DWORD cbWritten = 0;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BOOL bCreatedFile = FALSE;

    hr = ResReadData(NULL, szDataName, &pData, &cbData);
    ResrExitOnFailure(hr, "Failed to GetData from %s.", szDataName);

    hFile = ::CreateFileW(wzTargetFile, GENERIC_WRITE, 0, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        ResrExitWithLastError(hr, "Failed to CreateFileW for %ls.", wzTargetFile);
    }
    bCreatedFile = TRUE;

    if (!::WriteFile(hFile, pData, cbData, &cbWritten, NULL))
    {
        ResrExitWithLastError(hr, "Failed to ::WriteFile for %ls.", wzTargetFile);
    }

LExit:
    ReleaseFile(hFile);

    if (FAILED(hr))
    {
        if (bCreatedFile)
        {
            ::DeleteFileW(wzTargetFile);
        }
    }

    return hr;
}


BOOL CALLBACK EnumLangIdProc(
    __in_opt HMODULE /* hModule */,
    __in_z LPCSTR /* lpType */,
    __in_z LPCSTR /* lpName */,
    __in WORD wLanguage,
    __in LONG_PTR lParam
    )
{
    WORD *pwLangId = reinterpret_cast<WORD*>(lParam);

    *pwLangId = wLanguage;
    return TRUE;
}
