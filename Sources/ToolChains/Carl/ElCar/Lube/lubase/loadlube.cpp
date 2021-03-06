//=========================================================================
// Copyright (C) 2012 The Elastos Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//=========================================================================

#include <stdlib.h>
#ifndef _apple
#ifdef _linux
#include <sys/io.h>
#else
#include <io.h>
#include <windows.h>
#endif
#include <malloc.h>
#include <elf.h>
#endif

#include "lube.h"

#ifdef _ELASTOS64
    #define Elf32_64_Ehdr   Elf64_Ehdr
    #define Elf32_64_Shdr   Elf64_Shdr
#else
    #define Elf32_64_Ehdr   Elf32_Ehdr
    #define Elf32_64_Shdr   Elf32_Shdr
#endif

#ifndef _win32
#define _MAX_PATH 256
#endif

static const char *s_pszLubePath = "";
const char *g_pszOutputPath = "";
static size_t s_nOffset;

static const CLSID CLSID_XOR_CallbackSink = \
/* e724df56-e16a-4599-8edd-a97ab245d583 */
{0xe724df56,0xe16a,0x4599,{0x8e,0xdd,0xa9,0x7a,0xb2,0x45,0xd5,0x83}};

extern int AddCLSLibrary(CLSModule *);
extern int RetrieveClass(const char *, CLSModule *, BOOL);
extern int RetrieveInterface(const char *, CLSModule *, BOOL);
extern int RetrieveStruct(const char *, CLSModule *, BOOL);
extern int RetrieveEnum(const char *, const char *, CLSModule *, BOOL);
extern int RetrieveAlias(const char *, CLSModule *, BOOL);
extern int MergeCLS(const CLSModule *, CLSModule *);
extern int RetrieveIdentifyType(
                const char *, const char *, CLSModule *, TypeDescriptor *, BOOL);
extern void DestroyAllLibraries();

void RelocateState(PSTATEDESC pDesc)
{
    if (pDesc->pBlockette) {
        pDesc->pBlockette = (PSTATEDESC)((size_t)pDesc->pBlockette + s_nOffset);
        RelocateState(pDesc->pBlockette);
    }
    if (pDesc->pNext) {
        pDesc->pNext = (PSTATEDESC)((size_t)pDesc->pNext + s_nOffset);
        RelocateState(pDesc->pNext);
    }
    if (pDesc->pvData) {
        pDesc->pvData = (PVOID)((size_t)pDesc->pvData + s_nOffset);
    }
}

void RelocateTemplate(LubeTemplate *pTemplate)
{
    pTemplate->mName = (char *)((size_t)pTemplate->mName + s_nOffset);
    if (pTemplate->tRoot.pBlockette) {
        pTemplate->tRoot.pBlockette = (PSTATEDESC)
                    ((size_t)pTemplate->tRoot.pBlockette + s_nOffset);
        RelocateState(pTemplate->tRoot.pBlockette);
    }
}

void RelocateLube(PLUBEHEADER pLube)
{
    int n;

    s_nOffset = (size_t)pLube;

    pLube->ppTemplates = (LubeTemplate **)
                ((size_t)pLube->ppTemplates + s_nOffset);
    for (n = 0; n < pLube->cTemplates; n++) {
        pLube->ppTemplates[n] = (LubeTemplate *)
                    ((size_t)pLube->ppTemplates[n] + s_nOffset);
        RelocateTemplate(pLube->ppTemplates[n]);
    }
}

int RelocFlattedLube(PLUBEHEADER pSrc, int nSize, PLUBEHEADER *ppDest)
{
    PLUBEHEADER pDest;

    if (pSrc->dwMagic != c_dwLubeMagic) {
        fprintf(stderr, "[ERROR] lube (0x0301) : Illegal format of resource.\n");
        return LUBE_FAIL;
    }
    pDest = (PLUBEHEADER)new char[nSize];
    if (!pDest) {
        fprintf(stderr, "[ERROR] lube (0x0302) : Out of memory.\n");
        return LUBE_FAIL;
    }
    memcpy(pDest, pSrc, nSize);

    RelocateLube(pDest);

    *ppDest = pDest;
    return LUBE_OK;
}

void SetLubePath(const char *pszPath)
{
    s_pszLubePath = pszPath;
}

void SetOutputPath(const char *pszPath)
{
    g_pszOutputPath = pszPath;
}

#ifdef _win32
int LoadLubeFromDll(const char *pszName, PLUBEHEADER *ppLube)
{
    int nSize;
    HRSRC hResInfo;
    HGLOBAL hRes;
    LPVOID lpLockRes;
    HMODULE handle;

    if (pszName) {
        handle = LoadLibraryExA(pszName, NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (!handle) goto ErrorExit;
    }
    else {
        handle = NULL;
    }

    hResInfo = FindResourceA(handle, "#1", "Lube");
    if (!hResInfo) goto ErrorExit;

    nSize = SizeofResource(handle, hResInfo);
    if (0 == nSize) goto ErrorExit;

    hRes = LoadResource(handle, hResInfo);
    if (!hRes) goto ErrorExit;

    lpLockRes = LockResource(hRes);
    if (!lpLockRes) goto ErrorExit;

    return RelocFlattedLube((PLUBEHEADER)lpLockRes, nSize, ppLube);

ErrorExit:
    fprintf(stderr, "[ERROR] lube (0x0303 : Can't load lube resource from file.\n");
    return LUBE_FAIL;
}
#endif

typedef struct ModuleRscStruct {
    unsigned int    uSize;
	const char      uClsinfo[0];
} ModuleRscStruct;

#ifdef _linux
int LoadLubeFromELF(const char *pszName, PLUBEHEADER *ppLube)
{
    Elf32_64_Ehdr   ehdr;
    Elf32_64_Shdr   *shdr         = NULL;
    Elf32_64_Shdr   *pTemShdr     = NULL;
    ModuleRscStruct *pRsc         = NULL;
    FILE            *pFile        = NULL;
    char            *pStringTable = NULL;
    char            *pSecName     = NULL;
    char            *pGetSec      = NULL;
    char            *secHeader    = NULL;
    int             cnt           = 0;
    // int             rRet          = 0;
    int             nRet          = CLS_NoError;

    if (pszName == NULL) {
        char path[256];
        strcpy(path, getenv("XDK_TOOLS"));
        strcat(path, "/lube");
        pFile = fopen(path, "rb");
    }
    else {
        pFile = fopen(pszName, "rb");
    }

    if (!pFile) {
        return LUBE_FAIL;
    }

    if (fseek(pFile, 0, SEEK_SET) < 0) {
        return LUBE_FAIL;
    }

    if ((cnt = fread((char *)&ehdr, 1, sizeof(Elf32_64_Ehdr), pFile)) < 0) {
        return LUBE_FAIL;
    }

    if (fseek(pFile, ehdr.e_shoff, SEEK_SET) < 0) {
        return LUBE_FAIL;
    }

    secHeader = (char *)malloc(sizeof(Elf32_64_Shdr) * ehdr.e_shnum);

    if(NULL == secHeader){
        nRet = LUBE_FAIL;
        goto reterr;
    }

    if ((cnt = fread(secHeader, 1, ehdr.e_shnum * sizeof(Elf32_64_Shdr), pFile)) < 0) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    shdr         = (Elf32_64_Shdr *)secHeader;
    pTemShdr     = shdr + ehdr.e_shstrndx;
    pStringTable = (char *)malloc(pTemShdr->sh_size);

    if (fseek(pFile, pTemShdr->sh_offset, SEEK_SET) < 0) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    if ((cnt = fread(pStringTable, 1, pTemShdr->sh_size, pFile)) < 0) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    pTemShdr = shdr;

    for (cnt=0; cnt<ehdr.e_shnum; cnt++){
        pSecName = pStringTable + pTemShdr->sh_name;

        if (!strncmp(pSecName, ".clsresource", 12)) {
            break;
        }

        pTemShdr ++;
    }

    if (cnt == ehdr.e_shnum) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    pGetSec = (char *)malloc(pTemShdr->sh_size);

    if(NULL == pGetSec){
        nRet = LUBE_FAIL;
        goto reterr;
    }

    if (fseek(pFile, pTemShdr->sh_offset, SEEK_SET) < 0) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    if ((cnt = fread(pGetSec, 1, pTemShdr->sh_size, pFile)) < 0) {
        nRet = LUBE_FAIL;
        goto reterr;
    }

    fclose(pFile);
    pRsc = (ModuleRscStruct *)pGetSec;
//    rRet    = IsValidCLS((CLSModule *)&(pRsc->uClsinfo[0]), pRsc->uSize, pszName);
//    nRet = (rRet < 0) ? rRet : RelocFlattedCLS((CLSModule *)&(pRsc->uClsinfo[0]), pRsc->uSize, ppDest);

    nRet = RelocFlattedLube((PLUBEHEADER)&(pRsc->uClsinfo[0]), pRsc->uSize, ppLube);

reterr:
    if (secHeader)
        free((void *)secHeader);
    if (pGetSec)
        free(pGetSec);
    if(pStringTable)
        free(pStringTable);
    return nRet;
}
#endif

int LoadLubeFromFile(const char *pszName, PLUBEHEADER *ppLube)
{
    size_t nSize;
    int nRet = LUBE_FAIL;
    FILE *pFile;
    PLUBEHEADER pLube;

    pFile = fopen(pszName, "rb");
    if (!pFile) {
        fprintf(stderr, "[ERROR] lube (0x0304 : Can't open file %s.\n", pszName);
        return LUBE_FAIL;
    }

#ifndef _win32
    fseek(pFile, 0, SEEK_END);
    nSize = ftell(pFile);
    rewind(pFile);
#else
    nSize = _filelength(pFile->_file);
#endif
    pLube = (PLUBEHEADER)new char[nSize];
    if (!pLube) {
        fprintf(stderr, "[ERROR] lube (0x0305 : Out of memory.\n");
        goto ErrorExit;
    }
    if (fread(pLube, 1, nSize, pFile) != nSize) {
        fprintf(stderr, "[ERROR] lube (0x0304 : Can't open file %s.\n", pszName);
        return LUBE_FAIL;
    }

    nRet = RelocFlattedLube(pLube, nSize, ppLube);
    delete [] (char *)pLube;

ErrorExit:
    fclose(pFile);
    return nRet;
}

#ifdef _cmake
int LoadLubeFromCLS(const char *pszName, PLUBEHEADER *ppLube)
{
    if (pszName == NULL) {
        char path[256];
        strcpy(path, getenv("XDK_TOOLS"));
        strcat(path, "/lube.lbo");
        return LoadLubeFromFile(path, ppLube);
    }
    fprintf(stderr, "[ERROR] lube (0x0306 : Only support load lube.lbo for now.\n", pszName);
    return LUBE_FAIL;
}
#endif

int LoadLube(const char *pszName, PLUBEHEADER *ppLube)
{
    int n;
    char szResult[_MAX_PATH];

    if (!pszName) {
#if defined(_cmake)
        return LoadLubeFromCLS(NULL, ppLube);
#elif defined(_linux)
    	return LoadLubeFromELF(NULL, ppLube);
#elif defined(_win32)
    	return LoadLubeFromDll(NULL, ppLube);
#else
    #error Not supported platform!
#endif
    }

    n = SearchFileFromPath(s_pszLubePath, pszName, szResult);
    if (n < 0) return n;

    n = strlen(pszName);

    if (!_stricmp(pszName + n - 4, ".lbo")) {
        return LoadLubeFromFile(szResult, ppLube);
    }
    else if (!_stricmp(pszName + n - 4, ".eco")) {
#if defined(_linux)
        return LoadLubeFromELF(szResult, ppLube);
#elif defined(_win32)
        return LoadLubeFromDll(szResult, ppLube);
#else
        fprintf(stderr, "Do not support load Elastos meta data from shared object file %s.\n", pszName);
        return LUBE_FAIL;
#endif
    }
    else if (!_stricmp(pszName + n - 3, ".so")) {
#ifdef _linux
        return LoadLubeFromELF(szResult, ppLube);
#else
        fprintf(stderr, "Do not support load Elastos meta data from shared object file %s.\n", pszName);
        return LUBE_FAIL;
#endif
    }
    return LUBE_FAIL;
}

