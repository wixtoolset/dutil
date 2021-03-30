// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

#include "precomp.h"

// Exit macros
#define ApupExitOnLastError(x, s, ...) ExitOnLastErrorSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitOnLastErrorDebugTrace(x, s, ...) ExitOnLastErrorDebugTraceSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitWithLastError(x, s, ...) ExitWithLastErrorSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitOnFailure(x, s, ...) ExitOnFailureSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitOnRootFailure(x, s, ...) ExitOnRootFailureSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitOnFailureDebugTrace(x, s, ...) ExitOnFailureDebugTraceSource(DUTIL_SOURCE_APUPUTIL, x, s, __VA_ARGS__)
#define ApupExitOnNull(p, x, e, s, ...) ExitOnNullSource(DUTIL_SOURCE_APUPUTIL, p, x, e, s, __VA_ARGS__)
#define ApupExitOnNullWithLastError(p, x, s, ...) ExitOnNullWithLastErrorSource(DUTIL_SOURCE_APUPUTIL, p, x, s, __VA_ARGS__)
#define ApupExitOnNullDebugTrace(p, x, e, s, ...)  ExitOnNullDebugTraceSource(DUTIL_SOURCE_APUPUTIL, p, x, e, s, __VA_ARGS__)
#define ApupExitOnInvalidHandleWithLastError(p, x, s, ...) ExitOnInvalidHandleWithLastErrorSource(DUTIL_SOURCE_APUPUTIL, p, x, s, __VA_ARGS__)
#define ApupExitOnWin32Error(e, x, s, ...) ExitOnWin32ErrorSource(DUTIL_SOURCE_APUPUTIL, e, x, s, __VA_ARGS__)
#define ApupExitOnGdipFailure(g, x, s, ...) ExitOnGdipFailureSource(DUTIL_SOURCE_APUPUTIL, g, x, s, __VA_ARGS__)

// prototypes
static HRESULT ProcessEntry(
    __in ATOM_ENTRY* pAtomEntry,
    __in LPCWSTR wzDefaultAppId,
    __inout APPLICATION_UPDATE_ENTRY* pApupEntry
    );
static HRESULT ParseEnclosure(
    __in ATOM_LINK* pLink,
    __in APPLICATION_UPDATE_ENCLOSURE* pEnclosure
    );
static __callback int __cdecl CompareEntries(
    void* pvContext,
    const void* pvLeft,
    const void* pvRight
    );
static HRESULT FilterEntries(
    __in APPLICATION_UPDATE_ENTRY* rgEntries,
    __in DWORD cEntries,
    __in VERUTIL_VERSION* pCurrentVersion,
    __inout APPLICATION_UPDATE_ENTRY** prgFilteredEntries,
    __inout DWORD* pcFilteredEntries
    );
static HRESULT CopyEntry(
    __in const APPLICATION_UPDATE_ENTRY* pSrc,
    __in APPLICATION_UPDATE_ENTRY* pDest
    );
static HRESULT CopyEnclosure(
    __in const APPLICATION_UPDATE_ENCLOSURE* pSrc,
    __in APPLICATION_UPDATE_ENCLOSURE* pDest
    );
static void FreeEntry(
    __in APPLICATION_UPDATE_ENTRY* pApupEntry
    );
static void FreeEnclosure(
    __in APPLICATION_UPDATE_ENCLOSURE* pEnclosure
    );


//
// ApupCalculateChainFromAtom - returns the chain of application updates found in an ATOM feed.
//
extern "C" HRESULT DAPI ApupAllocChainFromAtom(
    __in ATOM_FEED* pFeed,
    __out APPLICATION_UPDATE_CHAIN** ppChain
    )
{
    HRESULT hr = S_OK;
    APPLICATION_UPDATE_CHAIN* pChain = NULL;

    pChain = static_cast<APPLICATION_UPDATE_CHAIN*>(MemAlloc(sizeof(APPLICATION_UPDATE_CHAIN), TRUE));

    // First search the ATOM feed's custom elements to try and find the default application identity.
    for (ATOM_UNKNOWN_ELEMENT* pElement = pFeed->pUnknownElements; pElement; pElement = pElement->pNext)
    {
        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzNamespace, -1, APPLICATION_SYNDICATION_NAMESPACE, -1))
        {
            if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzElement, -1, L"application", -1))
            {
                hr = StrAllocString(&pChain->wzDefaultApplicationId, pElement->wzValue, 0);
                ApupExitOnFailure(hr, "Failed to allocate default application id.");

                for (ATOM_UNKNOWN_ATTRIBUTE* pAttribute = pElement->pAttributes; pAttribute; pAttribute = pAttribute->pNext)
                {
                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pAttribute->wzAttribute, -1, L"type", -1))
                    {
                        hr = StrAllocString(&pChain->wzDefaultApplicationType, pAttribute->wzValue, 0);
                        ApupExitOnFailure(hr, "Failed to allocate default application type.");
                    }
                }
            }
        }
    }

    // Assume there will be as many application updates entries as their are feed entries.
    if (pFeed->cEntries)
    {
        pChain->rgEntries = static_cast<APPLICATION_UPDATE_ENTRY*>(MemAlloc(sizeof(APPLICATION_UPDATE_ENTRY) * pFeed->cEntries, TRUE));
        ApupExitOnNull(pChain->rgEntries, hr, E_OUTOFMEMORY, "Failed to allocate memory for update entries.");

        // Process each entry, building up the chain.
        for (DWORD i = 0; i < pFeed->cEntries; ++i)
        {
            hr = ProcessEntry(pFeed->rgEntries + i, pChain->wzDefaultApplicationId, pChain->rgEntries + pChain->cEntries);
            ApupExitOnFailure(hr, "Failed to process ATOM entry.");

            if (S_FALSE != hr)
            {
                ++pChain->cEntries;
            }
        }

        // Sort the chain by descending version and ascending total size.
        qsort_s(pChain->rgEntries, pChain->cEntries, sizeof(APPLICATION_UPDATE_ENTRY), CompareEntries, NULL);
    }

    // Trim the unused entries from the end, if any of the entries failed to parse or validate
    if (pChain->cEntries != pFeed->cEntries) 
    {
        if (pChain->cEntries > 0)
        {
            pChain->rgEntries = static_cast<APPLICATION_UPDATE_ENTRY*>(MemReAlloc(pChain->rgEntries, sizeof(APPLICATION_UPDATE_ENTRY) * pChain->cEntries, FALSE));
            ApupExitOnNull(pChain->rgEntries, hr, E_OUTOFMEMORY, "Failed to reallocate memory for update entries.");
        }
        else
        {
            ReleaseNullMem(pChain->rgEntries);
        }
    }

    *ppChain = pChain;
    pChain = NULL;

LExit:
    ReleaseApupChain(pChain);

    return hr;
}


//
// ApupFilterChain - remove the unneeded update elements from the chain.
//
HRESULT DAPI ApupFilterChain(
    __in APPLICATION_UPDATE_CHAIN* pChain,
    __in VERUTIL_VERSION* pVersion,
    __out APPLICATION_UPDATE_CHAIN** ppFilteredChain
    )
{
    HRESULT hr = S_OK;
    APPLICATION_UPDATE_CHAIN* pNewChain = NULL;
    APPLICATION_UPDATE_ENTRY* prgEntries = NULL;
    DWORD cEntries = NULL;

    pNewChain = static_cast<APPLICATION_UPDATE_CHAIN*>(MemAlloc(sizeof(APPLICATION_UPDATE_CHAIN), TRUE));
    ApupExitOnNull(pNewChain, hr, E_OUTOFMEMORY, "Failed to allocate filtered chain.");

    hr = FilterEntries(pChain->rgEntries, pChain->cEntries, pVersion, &prgEntries, &cEntries);
    ApupExitOnFailure(hr, "Failed to filter entries by version.");

    if (pChain->wzDefaultApplicationId)
    {
        hr = StrAllocString(&pNewChain->wzDefaultApplicationId, pChain->wzDefaultApplicationId, 0);
        ApupExitOnFailure(hr, "Failed to copy default application id.");
    }

    if (pChain->wzDefaultApplicationType)
    {
        hr = StrAllocString(&pNewChain->wzDefaultApplicationType, pChain->wzDefaultApplicationType, 0);
        ApupExitOnFailure(hr, "Failed to copy default application type.");
    }

    pNewChain->rgEntries = prgEntries;
    pNewChain->cEntries = cEntries;

    *ppFilteredChain = pNewChain;
    pNewChain = NULL;

LExit:
    ReleaseApupChain(pNewChain);
    return hr;
}


//
// ApupFreeChain - frees a previously allocated application update chain.
//
extern "C" void DAPI ApupFreeChain(
    __in APPLICATION_UPDATE_CHAIN* pChain
    )
{
    if (pChain)
    {
        for (DWORD i = 0; i < pChain->cEntries; ++i)
        {
            FreeEntry(pChain->rgEntries + i);
        }

        ReleaseMem(pChain->rgEntries);
        ReleaseStr(pChain->wzDefaultApplicationType);
        ReleaseStr(pChain->wzDefaultApplicationId);
        ReleaseMem(pChain);
    }
}


static HRESULT ProcessEntry(
    __in ATOM_ENTRY* pAtomEntry,
    __in LPCWSTR wzDefaultAppId,
    __inout APPLICATION_UPDATE_ENTRY* pApupEntry
    )
{
    HRESULT hr = S_OK;
    int nCompareResult = 0;

    // First search the ATOM entry's custom elements to try and find the application update information.
    for (ATOM_UNKNOWN_ELEMENT* pElement = pAtomEntry->pUnknownElements; pElement; pElement = pElement->pNext)
    {
        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzNamespace, -1, APPLICATION_SYNDICATION_NAMESPACE, -1))
        {
            if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzElement, -1, L"application", -1))
            {
                hr = StrAllocString(&pApupEntry->wzApplicationId, pElement->wzValue, 0);
                ApupExitOnFailure(hr, "Failed to allocate application identity.");

                for (ATOM_UNKNOWN_ATTRIBUTE* pAttribute = pElement->pAttributes; pAttribute; pAttribute = pAttribute->pNext)
                {
                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pAttribute->wzAttribute, -1, L"type", -1))
                    {
                        hr = StrAllocString(&pApupEntry->wzApplicationType, pAttribute->wzValue, 0);
                        ApupExitOnFailure(hr, "Failed to allocate application type.");
                    }
                }
            }
            else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzElement, -1, L"upgrade", -1))
            {
                hr = StrAllocString(&pApupEntry->wzUpgradeId, pElement->wzValue, 0);
                ApupExitOnFailure(hr, "Failed to allocate upgrade id.");

                for (ATOM_UNKNOWN_ATTRIBUTE* pAttribute = pElement->pAttributes; pAttribute; pAttribute = pAttribute->pNext)
                {
                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pAttribute->wzAttribute, -1, L"version", -1))
                    {
                        hr = VerParseVersion(pAttribute->wzValue, 0, FALSE, &pApupEntry->pUpgradeVersion);
                        ApupExitOnFailure(hr, "Failed to parse upgrade version string '%ls' from ATOM entry.", pAttribute->wzValue);
                    }
                    else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pAttribute->wzAttribute, -1, L"exclusive", -1))
                    {
                        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pAttribute->wzValue, -1, L"true", -1))
                        {
                            pApupEntry->fUpgradeExclusive = TRUE;
                        }
                    }
                }
            }
            else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzElement, -1, L"version", -1))
            {
                hr = VerParseVersion(pElement->wzValue, 0, FALSE, &pApupEntry->pVersion);
                ApupExitOnFailure(hr, "Failed to parse version string '%ls' from ATOM entry.", pElement->wzValue);
            }
        }
    }

    // If there is no application identity or no version, skip the whole thing.
    if ((!pApupEntry->wzApplicationId && !wzDefaultAppId) || !pApupEntry->pVersion)
    {
        ExitFunction1(hr = S_FALSE); // skip this update since it has no application id or version.
    }

    if (pApupEntry->pUpgradeVersion)
    {
        hr = VerCompareParsedVersions(pApupEntry->pUpgradeVersion, pApupEntry->pVersion, &nCompareResult);
        ApupExitOnFailure(hr, "Failed to compare version to upgrade version.");

        if (nCompareResult >= 0)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            ApupExitOnRootFailure(hr, "Upgrade version is greater than or equal to application version.");
        }
    }

    if (pAtomEntry->wzTitle)
    {
        hr = StrAllocString(&pApupEntry->wzTitle, pAtomEntry->wzTitle, 0);
        ApupExitOnFailure(hr, "Failed to allocate application title.");
    }

    if (pAtomEntry->wzSummary)
    {
        hr = StrAllocString(&pApupEntry->wzSummary, pAtomEntry->wzSummary, 0);
        ApupExitOnFailure(hr, "Failed to allocate application summary.");
    }

    if (pAtomEntry->pContent)
    {
        if (pAtomEntry->pContent->wzType)
        {
            hr = StrAllocString(&pApupEntry->wzContentType, pAtomEntry->pContent->wzType, 0);
            ApupExitOnFailure(hr, "Failed to allocate content type.");
        }

        if (pAtomEntry->pContent->wzValue)
        {
            hr = StrAllocString(&pApupEntry->wzContent, pAtomEntry->pContent->wzValue, 0);
            ApupExitOnFailure(hr, "Failed to allocate content.");
        }
    }
    // Now process the enclosures.  Assume every link in the ATOM entry is an enclosure.
    pApupEntry->rgEnclosures = static_cast<APPLICATION_UPDATE_ENCLOSURE*>(MemAlloc(sizeof(APPLICATION_UPDATE_ENCLOSURE) * pAtomEntry->cLinks, TRUE));
    ApupExitOnNull(pApupEntry->rgEnclosures, hr, E_OUTOFMEMORY, "Failed to allocate enclosures for application update entry.");

    for (DWORD i = 0; i < pAtomEntry->cLinks; ++i)
    {
        ATOM_LINK* pLink = pAtomEntry->rgLinks + i;
        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pLink->wzRel, -1, L"enclosure", -1))
        {
            hr = ParseEnclosure(pLink, pApupEntry->rgEnclosures + pApupEntry->cEnclosures);
            ApupExitOnFailure(hr, "Failed to parse enclosure.");

            pApupEntry->dw64TotalSize += pApupEntry->rgEnclosures[pApupEntry->cEnclosures].dw64Size; // total up the size of the enclosures

            ++pApupEntry->cEnclosures;
        }
    }

LExit:
    if (S_OK != hr) // if anything went wrong, free the entry.
    {
        FreeEntry(pApupEntry);
        memset(pApupEntry, 0, sizeof(APPLICATION_UPDATE_ENTRY));
    }

    return hr;
}


static HRESULT ParseEnclosure(
    __in ATOM_LINK* pLink,
    __in APPLICATION_UPDATE_ENCLOSURE* pEnclosure
    )
{
    HRESULT hr = S_OK;
    DWORD dwDigestLength = 0;
    DWORD dwDigestStringLength = 0;
    size_t cchDigestString = 0;

    // First search the ATOM link's custom elements to try and find the application update enclosure information.
    for (ATOM_UNKNOWN_ELEMENT* pElement = pLink->pUnknownElements; pElement; pElement = pElement->pNext)
    {
        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, pElement->wzNamespace, -1, APPLICATION_SYNDICATION_NAMESPACE, -1))
        {
            if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, L"digest", -1, pElement->wzElement, -1))
            {
                // Find the digest[@algorithm] which is required. Everything else is ignored.
                for (ATOM_UNKNOWN_ATTRIBUTE* pAttribute = pElement->pAttributes; pAttribute; pAttribute = pAttribute->pNext)
                {
                    dwDigestLength = 0;
                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, L"algorithm", -1, pAttribute->wzAttribute, -1))
                    {
                        if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, L"md5", -1, pAttribute->wzValue, -1))
                        {
                            pEnclosure->digestAlgorithm = APUP_HASH_ALGORITHM_MD5;
                            dwDigestLength = MD5_HASH_LEN;
                        }
                        else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, L"sha1", -1, pAttribute->wzValue, -1))
                        {
                            pEnclosure->digestAlgorithm = APUP_HASH_ALGORITHM_SHA1;
                            dwDigestLength = SHA1_HASH_LEN;
                        }
                        else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, L"sha256", -1, pAttribute->wzValue, -1))
                        {
                            pEnclosure->digestAlgorithm = APUP_HASH_ALGORITHM_SHA256;
                            dwDigestLength = SHA256_HASH_LEN;
                        }
                        else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, L"sha512", -1, pAttribute->wzValue, -1))
                        {
                            pEnclosure->digestAlgorithm = APUP_HASH_ALGORITHM_SHA512;
                            dwDigestLength = SHA512_HASH_LEN;
                        }
                        break;
                    }
                }

                if (dwDigestLength)
                {
                    dwDigestStringLength = 2 * dwDigestLength;

                    hr = ::StringCchLengthW(pElement->wzValue, STRSAFE_MAX_CCH, &cchDigestString);
                    ApupExitOnFailure(hr, "Failed to get string length of digest value.");

                    if (dwDigestStringLength != cchDigestString)
                    {
                        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                        ApupExitOnRootFailure(hr, "Invalid digest length (%zu) for digest algorithm (%u).", cchDigestString, dwDigestStringLength);
                    }

                    pEnclosure->cbDigest = sizeof(BYTE) * dwDigestLength;
                    pEnclosure->rgbDigest = static_cast<BYTE*>(MemAlloc(pEnclosure->cbDigest, TRUE));
                    ApupExitOnNull(pEnclosure->rgbDigest, hr, E_OUTOFMEMORY, "Failed to allocate memory for digest.");

                    hr = StrHexDecode(pElement->wzValue, pEnclosure->rgbDigest, pEnclosure->cbDigest);
                    ApupExitOnFailure(hr, "Failed to decode digest value.");
                }
                else
                {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    ApupExitOnRootFailure(hr, "Unknown algorithm type for digest.");
                }

                break;
            }
            else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, 0, L"name", -1, pElement->wzElement, -1))
            {
                hr = StrAllocString(&pEnclosure->wzLocalName, pElement->wzValue, 0);
                ApupExitOnFailure(hr, "Failed to copy local name.");
            }
        }
    }

    pEnclosure->dw64Size = pLink->dw64Length;

    hr = StrAllocString(&pEnclosure->wzUrl, pLink->wzUrl, 0);
    ApupExitOnFailure(hr, "Failed to allocate enclosure URL.");

    pEnclosure->fInstaller = FALSE;
    pEnclosure->wzLocalName = NULL;

LExit:
    return hr;
}


static __callback int __cdecl CompareEntries(
    void* /*pvContext*/,
    const void* pvLeft,
    const void* pvRight
    )
{
    int ret = 0;
    const APPLICATION_UPDATE_ENTRY* pEntryLeft = static_cast<const APPLICATION_UPDATE_ENTRY*>(pvLeft);
    const APPLICATION_UPDATE_ENTRY* pEntryRight = static_cast<const APPLICATION_UPDATE_ENTRY*>(pvRight);

    VerCompareParsedVersions(pEntryLeft->pVersion, pEntryRight->pVersion, &ret);
    if (0 == ret)
    {
        VerCompareParsedVersions(pEntryLeft->pUpgradeVersion, pEntryRight->pUpgradeVersion, &ret);
        if (0 == ret)
        {
            ret = (pEntryLeft->dw64TotalSize < pEntryRight->dw64TotalSize) ? -1 :
                  (pEntryLeft->dw64TotalSize > pEntryRight->dw64TotalSize) ?  1 : 0;
        }
    }

    // Sort descending.
    ret = -ret;

    return ret;
}


static HRESULT FilterEntries(
    __in APPLICATION_UPDATE_ENTRY* rgEntries,
    __in DWORD cEntries,
    __in VERUTIL_VERSION* pCurrentVersion,
    __inout APPLICATION_UPDATE_ENTRY** prgFilteredEntries,
    __inout DWORD* pcFilteredEntries
    )
{
    HRESULT hr = S_OK;
    int nCompareResult = 0;
    size_t cbAllocSize = 0;
    const APPLICATION_UPDATE_ENTRY* pRequired = NULL;;
    LPVOID pv = NULL;

    if (cEntries)
    {
        for (DWORD i = 0; i < cEntries; ++i)
        {
            const APPLICATION_UPDATE_ENTRY* pEntry = rgEntries + i;

            hr = VerCompareParsedVersions(pCurrentVersion, pEntry->pVersion, &nCompareResult);
            ApupExitOnFailure(hr, "Failed to compare versions.");

            if (nCompareResult >= 0)
            {
                continue;
            }

            hr = VerCompareParsedVersions(pCurrentVersion, pEntry->pUpgradeVersion, &nCompareResult);
            ApupExitOnFailure(hr, "Failed to compare upgrade versions.");

            if (nCompareResult > 0 || (!pEntry->fUpgradeExclusive && nCompareResult == 0))
            {
                pRequired = pEntry;
                break;
            }
        }

        if (pRequired)
        {
            DWORD cNewFilteredEntries = *pcFilteredEntries + 1;

            hr = ::SizeTMult(sizeof(APPLICATION_UPDATE_ENTRY), cNewFilteredEntries, &cbAllocSize);
            ApupExitOnFailure(hr, "Overflow while calculating alloc size for more entries - number of entries: %u", cNewFilteredEntries);

            if (*prgFilteredEntries)
            {
                pv = MemReAlloc(*prgFilteredEntries, cbAllocSize, FALSE);
                ApupExitOnNull(pv, hr, E_OUTOFMEMORY, "Failed to reallocate memory for more entries.");
            }
            else
            {
                pv = MemAlloc(cbAllocSize, TRUE);
                ApupExitOnNull(pv, hr, E_OUTOFMEMORY, "Failed to allocate memory for entries.");
            }

            *pcFilteredEntries = cNewFilteredEntries;
            *prgFilteredEntries = static_cast<APPLICATION_UPDATE_ENTRY*>(pv);
            pv = NULL;

            hr = CopyEntry(pRequired, *prgFilteredEntries + *pcFilteredEntries - 1);
            ApupExitOnFailure(hr, "Failed to deep copy entry.");

            hr = VerCompareParsedVersions(pRequired->pVersion, rgEntries[0].pVersion, &nCompareResult);
            ApupExitOnFailure(hr, "Failed to compare required version.");

            if (nCompareResult < 0)
            {
                FilterEntries(rgEntries, cEntries, pRequired->pVersion, prgFilteredEntries, pcFilteredEntries);
            }
        }
    }

LExit:
    ReleaseMem(pv);
    return hr;
}


static HRESULT CopyEntry(
    __in const APPLICATION_UPDATE_ENTRY* pSrc,
    __in APPLICATION_UPDATE_ENTRY* pDest
    )
{
    HRESULT hr = S_OK;
    size_t cbAllocSize = 0;

    memset(pDest, 0, sizeof(APPLICATION_UPDATE_ENTRY));

    if (pSrc->wzApplicationId)
    {
        hr = StrAllocString(&pDest->wzApplicationId, pSrc->wzApplicationId, 0);
        ApupExitOnFailure(hr, "Failed to copy application id.");
    }

    if (pSrc->wzApplicationType)
    {
        hr = StrAllocString(&pDest->wzApplicationType, pSrc->wzApplicationType, 0);
        ApupExitOnFailure(hr, "Failed to copy application type.");
    }

    if (pSrc->wzUpgradeId)
    {
        hr = StrAllocString(&pDest->wzUpgradeId, pSrc->wzUpgradeId, 0);
        ApupExitOnFailure(hr, "Failed to copy upgrade id.");
    }

    if (pSrc->wzTitle)
    {
        hr = StrAllocString(&pDest->wzTitle, pSrc->wzTitle, 0);
        ApupExitOnFailure(hr, "Failed to copy title.");
    }

    if (pSrc->wzSummary)
    {
        hr = StrAllocString(&pDest->wzSummary, pSrc->wzSummary, 0);
        ApupExitOnFailure(hr, "Failed to copy summary.");
    }

    if (pSrc->wzContentType)
    {
        hr = StrAllocString(&pDest->wzContentType, pSrc->wzContentType, 0);
        ApupExitOnFailure(hr, "Failed to copy content type.");
    }

    if (pSrc->wzContent)
    {
        hr = StrAllocString(&pDest->wzContent, pSrc->wzContent, 0);
        ApupExitOnFailure(hr, "Failed to copy content.");
    }

    pDest->dw64TotalSize = pSrc->dw64TotalSize;

    hr = VerCopyVersion(pSrc->pUpgradeVersion, &pDest->pUpgradeVersion);
    ApupExitOnFailure(hr, "Failed to copy upgrade version.");

    hr = VerCopyVersion(pSrc->pVersion, &pDest->pVersion);
    ApupExitOnFailure(hr, "Failed to copy version.");

    pDest->fUpgradeExclusive = pSrc->fUpgradeExclusive;

    hr = ::SizeTMult(sizeof(APPLICATION_UPDATE_ENCLOSURE), pSrc->cEnclosures, &cbAllocSize);
    ApupExitOnRootFailure(hr, "Overflow while calculating memory allocation size");

    pDest->rgEnclosures = static_cast<APPLICATION_UPDATE_ENCLOSURE*>(MemAlloc(cbAllocSize, TRUE));
    ApupExitOnNull(pDest->rgEnclosures, hr, E_OUTOFMEMORY, "Failed to allocate copy of enclosures.");

    pDest->cEnclosures = pSrc->cEnclosures;

    for (DWORD i = 0; i < pDest->cEnclosures; ++i)
    {
        hr = CopyEnclosure(pSrc->rgEnclosures + i, pDest->rgEnclosures + i);
        ApupExitOnFailure(hr, "Failed to copy enclosure.");
    }

LExit:
    if (FAILED(hr))
    {
        FreeEntry(pDest);
    }

    return hr;
}


static HRESULT CopyEnclosure(
    __in const APPLICATION_UPDATE_ENCLOSURE* pSrc,
    __in APPLICATION_UPDATE_ENCLOSURE* pDest
    )
{
    HRESULT hr = S_OK;

    memset(pDest, 0, sizeof(APPLICATION_UPDATE_ENCLOSURE));

    if (pSrc->wzUrl)
    {
        hr = StrAllocString(&pDest->wzUrl, pSrc->wzUrl, 0);
        ApupExitOnFailure(hr, "Failed copy url.");
    }

    if (pSrc->wzLocalName)
    {
        hr = StrAllocString(&pDest->wzLocalName, pSrc->wzLocalName, 0);
        ApupExitOnFailure(hr, "Failed copy url.");
    }

    pDest->rgbDigest = static_cast<BYTE*>(MemAlloc(sizeof(BYTE) * pSrc->cbDigest, FALSE));
    ApupExitOnNull(pDest->rgbDigest, hr, E_OUTOFMEMORY, "Failed to allocate memory for copy of digest.");

    pDest->cbDigest = pSrc->cbDigest;

    memcpy_s(pDest->rgbDigest, sizeof(BYTE) * pDest->cbDigest, pSrc->rgbDigest, sizeof(BYTE) * pSrc->cbDigest);

    pDest->digestAlgorithm = pSrc->digestAlgorithm;

    pDest->dw64Size = pSrc->dw64Size;
    pDest->fInstaller = pSrc->fInstaller;

LExit:
    if (FAILED(hr))
    {
        FreeEnclosure(pDest);
    }

    return hr;
}


static void FreeEntry(
    __in APPLICATION_UPDATE_ENTRY* pEntry
    )
{
    if (pEntry)
    {
        for (DWORD i = 0; i < pEntry->cEnclosures; ++i)
        {
            FreeEnclosure(pEntry->rgEnclosures + i);
        }

        ReleaseStr(pEntry->wzUpgradeId);
        ReleaseStr(pEntry->wzApplicationType);
        ReleaseStr(pEntry->wzApplicationId);
        ReleaseStr(pEntry->wzTitle);
        ReleaseStr(pEntry->wzSummary);
        ReleaseStr(pEntry->wzContentType);
        ReleaseStr(pEntry->wzContent);
        ReleaseVerutilVersion(pEntry->pVersion);
        ReleaseVerutilVersion(pEntry->pUpgradeVersion);
    }
}


static void FreeEnclosure(
    __in APPLICATION_UPDATE_ENCLOSURE* pEnclosure
    )
{
    if (pEnclosure)
    {
        ReleaseMem(pEnclosure->rgbDigest);
        ReleaseStr(pEnclosure->wzLocalName);
        ReleaseStr(pEnclosure->wzUrl);
    }
}
