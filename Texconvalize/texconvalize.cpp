//--------------------------------------------------------------------------------------
// File: TexConvalize.cpp
//
// DirectX Texture Converter
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248926
//--------------------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)

#include <ShlObj.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <memory>
#include <list>
#include <string>

#include <wrl\client.h>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgiformat.h>

#include <wincodec.h>

#pragma warning(disable : 4619 4616 26812)

#include "DirectXTex.h"

#include "DirectXPackedVector.h"

//Uncomment to add support for OpenEXR (.exr)
//#define USE_OPENEXR

#ifdef USE_OPENEXR
// See <https://github.com/Microsoft/DirectXTex/wiki/Adding-OpenEXR> for details
#include "DirectXTexEXR.h"
#endif

using namespace DirectX;
using namespace DirectX::PackedVector;
using Microsoft::WRL::ComPtr;



namespace
{
    enum OPTIONS
    {
        OPT_RECURSIVE = 1,
        OPT_FILELIST,
        OPT_WIDTH,
        OPT_HEIGHT,
        OPT_MIPLEVELS,
        OPT_FORMAT,
        OPT_FILTER,
        OPT_SRGBI,
        OPT_SRGBO,
        OPT_SRGB,
        OPT_PREFIX,
        OPT_SUFFIX,
        OPT_OUTPUTDIR,
        OPT_TOLOWER,
        OPT_OVERWRITE,
        OPT_FILETYPE,
        OPT_HFLIP,
        OPT_VFLIP,
        OPT_DDS_DWORD_ALIGN,
        OPT_DDS_BAD_DXTN_TAILS,
        OPT_USE_DX10,
        OPT_USE_DX9,
        OPT_TGA20,
        OPT_WIC_QUALITY,
        OPT_WIC_LOSSLESS,
        OPT_WIC_MULTIFRAME,
        OPT_NOLOGO,
        OPT_TIMING,
        OPT_SEPALPHA,
        OPT_NO_WIC,
        OPT_TYPELESS_UNORM,
        OPT_TYPELESS_FLOAT,
        OPT_PREMUL_ALPHA,
        OPT_DEMUL_ALPHA,
        OPT_EXPAND_LUMINANCE,
        OPT_TA_WRAP,
        OPT_TA_MIRROR,
        OPT_FORCE_SINGLEPROC,
        OPT_GPU,
        OPT_NOGPU,
        OPT_FEATURE_LEVEL,
        OPT_FIT_POWEROF2,
        OPT_ALPHA_THRESHOLD,
        OPT_ALPHA_WEIGHT,
        OPT_NORMAL_MAP,
        OPT_NORMAL_MAP_AMPLITUDE,
        OPT_BC_COMPRESS,
        OPT_COLORKEY,
        OPT_TONEMAP,
        OPT_X2_BIAS,
        OPT_PRESERVE_ALPHA_COVERAGE,
        OPT_INVERT_Y,
        OPT_RECONSTRUCT_Z,
        OPT_ROTATE_COLOR,
        OPT_PAPER_WHITE_NITS,
        OPT_BCNONMULT4FIX,
        OPT_SWIZZLE,
        OPT_MAX
    };

    enum
    {
        ROTATE_709_TO_HDR10 = 1,
        ROTATE_HDR10_TO_709,
        ROTATE_709_TO_2020,
        ROTATE_2020_TO_709,
        ROTATE_P3_TO_HDR10,
        ROTATE_P3_TO_2020,
    };

    static_assert(OPT_MAX <= 64, "dwOptions is a DWORD64 bitfield");

    struct SConversion
    {
        wchar_t szSrc[MAX_PATH];
        wchar_t szFolder[MAX_PATH];
    };

    struct SValue
    {
        LPCWSTR pName;
        DWORD dwValue;
    };

    const SValue g_pOptions[] =
    {
        { L"r",             OPT_RECURSIVE },
        { L"flist",         OPT_FILELIST },
        { L"w",             OPT_WIDTH },
        { L"h",             OPT_HEIGHT },
        { L"m",             OPT_MIPLEVELS },
        { L"f",             OPT_FORMAT },
        { L"if",            OPT_FILTER },
        { L"srgbi",         OPT_SRGBI },
        { L"srgbo",         OPT_SRGBO },
        { L"srgb",          OPT_SRGB },
        { L"px",            OPT_PREFIX },
        { L"sx",            OPT_SUFFIX },
        { L"o",             OPT_OUTPUTDIR },
        { L"l",             OPT_TOLOWER },
        { L"y",             OPT_OVERWRITE },
        { L"ft",            OPT_FILETYPE },
        { L"hflip",         OPT_HFLIP },
        { L"vflip",         OPT_VFLIP },
        { L"dword",         OPT_DDS_DWORD_ALIGN },
        { L"badtails",      OPT_DDS_BAD_DXTN_TAILS },
        { L"dx10",          OPT_USE_DX10 },
        { L"dx9",           OPT_USE_DX9 },
        { L"tga20",         OPT_TGA20 },
        { L"wicq",          OPT_WIC_QUALITY },
        { L"wiclossless",   OPT_WIC_LOSSLESS },
        { L"wicmulti",      OPT_WIC_MULTIFRAME },
        { L"nologo",        OPT_NOLOGO },
        { L"timing",        OPT_TIMING },
        { L"sepalpha",      OPT_SEPALPHA },
        { L"keepcoverage",  OPT_PRESERVE_ALPHA_COVERAGE },
        { L"nowic",         OPT_NO_WIC },
        { L"tu",            OPT_TYPELESS_UNORM },
        { L"tf",            OPT_TYPELESS_FLOAT },
        { L"pmalpha",       OPT_PREMUL_ALPHA },
        { L"alpha",         OPT_DEMUL_ALPHA },
        { L"xlum",          OPT_EXPAND_LUMINANCE },
        { L"wrap",          OPT_TA_WRAP },
        { L"mirror",        OPT_TA_MIRROR },
        { L"singleproc",    OPT_FORCE_SINGLEPROC },
        { L"gpu",           OPT_GPU },
        { L"nogpu",         OPT_NOGPU },
        { L"fl",            OPT_FEATURE_LEVEL },
        { L"pow2",          OPT_FIT_POWEROF2 },
        { L"at",            OPT_ALPHA_THRESHOLD },
        { L"aw",            OPT_ALPHA_WEIGHT },
        { L"nmap",          OPT_NORMAL_MAP },
        { L"nmapamp",       OPT_NORMAL_MAP_AMPLITUDE },
        { L"bc",            OPT_BC_COMPRESS },
        { L"c",             OPT_COLORKEY },
        { L"tonemap",       OPT_TONEMAP },
        { L"x2bias",        OPT_X2_BIAS },
        { L"inverty",       OPT_INVERT_Y },
        { L"reconstructz",  OPT_RECONSTRUCT_Z },
        { L"rotatecolor",   OPT_ROTATE_COLOR },
        { L"nits",          OPT_PAPER_WHITE_NITS },
        { L"fixbc4x4",      OPT_BCNONMULT4FIX },
        { L"swizzle",       OPT_SWIZZLE },
        { nullptr,          0 }
    };

#define DEFFMT(fmt) { L## #fmt, DXGI_FORMAT_ ## fmt }

    const SValue g_pFormats[] =
    {
        // List does not include _TYPELESS or depth/stencil formats
        DEFFMT(R32G32B32A32_FLOAT),
        DEFFMT(R32G32B32A32_UINT),
        DEFFMT(R32G32B32A32_SINT),
        DEFFMT(R32G32B32_FLOAT),
        DEFFMT(R32G32B32_UINT),
        DEFFMT(R32G32B32_SINT),
        DEFFMT(R16G16B16A16_FLOAT),
        DEFFMT(R16G16B16A16_UNORM),
        DEFFMT(R16G16B16A16_UINT),
        DEFFMT(R16G16B16A16_SNORM),
        DEFFMT(R16G16B16A16_SINT),
        DEFFMT(R32G32_FLOAT),
        DEFFMT(R32G32_UINT),
        DEFFMT(R32G32_SINT),
        DEFFMT(R10G10B10A2_UNORM),
        DEFFMT(R10G10B10A2_UINT),
        DEFFMT(R11G11B10_FLOAT),
        DEFFMT(R8G8B8A8_UNORM),
        DEFFMT(R8G8B8A8_UNORM_SRGB),
        DEFFMT(R8G8B8A8_UINT),
        DEFFMT(R8G8B8A8_SNORM),
        DEFFMT(R8G8B8A8_SINT),
        DEFFMT(R16G16_FLOAT),
        DEFFMT(R16G16_UNORM),
        DEFFMT(R16G16_UINT),
        DEFFMT(R16G16_SNORM),
        DEFFMT(R16G16_SINT),
        DEFFMT(R32_FLOAT),
        DEFFMT(R32_UINT),
        DEFFMT(R32_SINT),
        DEFFMT(R8G8_UNORM),
        DEFFMT(R8G8_UINT),
        DEFFMT(R8G8_SNORM),
        DEFFMT(R8G8_SINT),
        DEFFMT(R16_FLOAT),
        DEFFMT(R16_UNORM),
        DEFFMT(R16_UINT),
        DEFFMT(R16_SNORM),
        DEFFMT(R16_SINT),
        DEFFMT(R8_UNORM),
        DEFFMT(R8_UINT),
        DEFFMT(R8_SNORM),
        DEFFMT(R8_SINT),
        DEFFMT(A8_UNORM),
        DEFFMT(R9G9B9E5_SHAREDEXP),
        DEFFMT(R8G8_B8G8_UNORM),
        DEFFMT(G8R8_G8B8_UNORM),
        DEFFMT(BC1_UNORM),
        DEFFMT(BC1_UNORM_SRGB),
        DEFFMT(BC2_UNORM),
        DEFFMT(BC2_UNORM_SRGB),
        DEFFMT(BC3_UNORM),
        DEFFMT(BC3_UNORM_SRGB),
        DEFFMT(BC4_UNORM),
        DEFFMT(BC4_SNORM),
        DEFFMT(BC5_UNORM),
        DEFFMT(BC5_SNORM),
        DEFFMT(B5G6R5_UNORM),
        DEFFMT(B5G5R5A1_UNORM),

        // DXGI 1.1 formats
        DEFFMT(B8G8R8A8_UNORM),
        DEFFMT(B8G8R8X8_UNORM),
        DEFFMT(R10G10B10_XR_BIAS_A2_UNORM),
        DEFFMT(B8G8R8A8_UNORM_SRGB),
        DEFFMT(B8G8R8X8_UNORM_SRGB),
        DEFFMT(BC6H_UF16),
        DEFFMT(BC6H_SF16),
        DEFFMT(BC7_UNORM),
        DEFFMT(BC7_UNORM_SRGB),

        // DXGI 1.2 formats
        DEFFMT(AYUV),
        DEFFMT(Y410),
        DEFFMT(Y416),
        DEFFMT(YUY2),
        DEFFMT(Y210),
        DEFFMT(Y216),
        // No support for legacy paletted video formats (AI44, IA44, P8, A8P8)
        DEFFMT(B4G4R4A4_UNORM),

        { nullptr, DXGI_FORMAT_UNKNOWN }
    };

    const SValue g_pFormatAliases[] =
    {
        { L"DXT1", DXGI_FORMAT_BC1_UNORM },
        { L"DXT2", DXGI_FORMAT_BC2_UNORM },
        { L"DXT3", DXGI_FORMAT_BC2_UNORM },
        { L"DXT4", DXGI_FORMAT_BC3_UNORM },
        { L"DXT5", DXGI_FORMAT_BC3_UNORM },

        { L"RGBA", DXGI_FORMAT_R8G8B8A8_UNORM },
        { L"BGRA", DXGI_FORMAT_B8G8R8A8_UNORM },

        { L"FP16", DXGI_FORMAT_R16G16B16A16_FLOAT },
        { L"FP32", DXGI_FORMAT_R32G32B32A32_FLOAT },

        { L"BPTC", DXGI_FORMAT_BC7_UNORM },
        { L"BPTC_FLOAT", DXGI_FORMAT_BC6H_UF16 },

        { nullptr, DXGI_FORMAT_UNKNOWN }
    };

    const SValue g_pReadOnlyFormats[] =
    {
        DEFFMT(R32G32B32A32_TYPELESS),
        DEFFMT(R32G32B32_TYPELESS),
        DEFFMT(R16G16B16A16_TYPELESS),
        DEFFMT(R32G32_TYPELESS),
        DEFFMT(R32G8X24_TYPELESS),
        DEFFMT(D32_FLOAT_S8X24_UINT),
        DEFFMT(R32_FLOAT_X8X24_TYPELESS),
        DEFFMT(X32_TYPELESS_G8X24_UINT),
        DEFFMT(R10G10B10A2_TYPELESS),
        DEFFMT(R8G8B8A8_TYPELESS),
        DEFFMT(R16G16_TYPELESS),
        DEFFMT(R32_TYPELESS),
        DEFFMT(D32_FLOAT),
        DEFFMT(R24G8_TYPELESS),
        DEFFMT(D24_UNORM_S8_UINT),
        DEFFMT(R24_UNORM_X8_TYPELESS),
        DEFFMT(X24_TYPELESS_G8_UINT),
        DEFFMT(R8G8_TYPELESS),
        DEFFMT(R16_TYPELESS),
        DEFFMT(R8_TYPELESS),
        DEFFMT(BC1_TYPELESS),
        DEFFMT(BC2_TYPELESS),
        DEFFMT(BC3_TYPELESS),
        DEFFMT(BC4_TYPELESS),
        DEFFMT(BC5_TYPELESS),

        // DXGI 1.1 formats
        DEFFMT(B8G8R8A8_TYPELESS),
        DEFFMT(B8G8R8X8_TYPELESS),
        DEFFMT(BC6H_TYPELESS),
        DEFFMT(BC7_TYPELESS),

        // DXGI 1.2 formats
        DEFFMT(NV12),
        DEFFMT(P010),
        DEFFMT(P016),
        DEFFMT(420_OPAQUE),
        DEFFMT(NV11),

        // DXGI 1.3 formats
        { L"P208", DXGI_FORMAT(130) },
        { L"V208", DXGI_FORMAT(131) },
        { L"V408", DXGI_FORMAT(132) },

        { nullptr, DXGI_FORMAT_UNKNOWN }
    };

    const SValue g_pFilters[] =
    {
        { L"POINT",                     TEX_FILTER_POINT },
        { L"LINEAR",                    TEX_FILTER_LINEAR },
        { L"CUBIC",                     TEX_FILTER_CUBIC },
        { L"FANT",                      TEX_FILTER_FANT },
        { L"BOX",                       TEX_FILTER_BOX },
        { L"TRIANGLE",                  TEX_FILTER_TRIANGLE },
        { L"POINT_DITHER",              TEX_FILTER_POINT | TEX_FILTER_DITHER },
        { L"LINEAR_DITHER",             TEX_FILTER_LINEAR | TEX_FILTER_DITHER },
        { L"CUBIC_DITHER",              TEX_FILTER_CUBIC | TEX_FILTER_DITHER },
        { L"FANT_DITHER",               TEX_FILTER_FANT | TEX_FILTER_DITHER },
        { L"BOX_DITHER",                TEX_FILTER_BOX | TEX_FILTER_DITHER },
        { L"TRIANGLE_DITHER",           TEX_FILTER_TRIANGLE | TEX_FILTER_DITHER },
        { L"POINT_DITHER_DIFFUSION",    TEX_FILTER_POINT | TEX_FILTER_DITHER_DIFFUSION },
        { L"LINEAR_DITHER_DIFFUSION",   TEX_FILTER_LINEAR | TEX_FILTER_DITHER_DIFFUSION },
        { L"CUBIC_DITHER_DIFFUSION",    TEX_FILTER_CUBIC | TEX_FILTER_DITHER_DIFFUSION },
        { L"FANT_DITHER_DIFFUSION",     TEX_FILTER_FANT | TEX_FILTER_DITHER_DIFFUSION },
        { L"BOX_DITHER_DIFFUSION",      TEX_FILTER_BOX | TEX_FILTER_DITHER_DIFFUSION },
        { L"TRIANGLE_DITHER_DIFFUSION", TEX_FILTER_TRIANGLE | TEX_FILTER_DITHER_DIFFUSION },
        { nullptr,                      TEX_FILTER_DEFAULT                              }
    };

    const SValue g_pRotateColor[] =
    {
        { L"709to2020", ROTATE_709_TO_2020 },
        { L"2020to709", ROTATE_2020_TO_709 },
        { L"709toHDR10", ROTATE_709_TO_HDR10 },
        { L"HDR10to709", ROTATE_HDR10_TO_709 },
        { L"P3to2020", ROTATE_P3_TO_2020 },
        { L"P3toHDR10", ROTATE_P3_TO_HDR10 },
        { nullptr, 0 },
    };

#define CODEC_DDS 0xFFFF0001
#define CODEC_TGA 0xFFFF0002
#define CODEC_HDP 0xFFFF0003
#define CODEC_JXR 0xFFFF0004
#define CODEC_HDR 0xFFFF0005
#define CODEC_PPM 0xFFFF0006
#define CODEC_PFM 0xFFFF0007

#ifdef USE_OPENEXR
#define CODEC_EXR 0xFFFF0008
#endif

    const SValue g_pSaveFileTypes[] =   // valid formats to write to
    {
        { L"BMP",   WIC_CODEC_BMP  },
        { L"JPG",   WIC_CODEC_JPEG },
        { L"JPEG",  WIC_CODEC_JPEG },
        { L"PNG",   WIC_CODEC_PNG  },
        { L"DDS",   CODEC_DDS      },
        { L"TGA",   CODEC_TGA      },
        { L"HDR",   CODEC_HDR      },
        { L"TIF",   WIC_CODEC_TIFF },
        { L"TIFF",  WIC_CODEC_TIFF },
        { L"WDP",   WIC_CODEC_WMP  },
        { L"HDP",   CODEC_HDP      },
        { L"JXR",   CODEC_JXR      },
        { L"PPM",   CODEC_PPM      },
        { L"PFM",   CODEC_PFM      },
    #ifdef USE_OPENEXR
        { L"EXR",   CODEC_EXR      },
    #endif
        { nullptr,  CODEC_DDS      }
    };

    const SValue g_pFeatureLevels[] =   // valid feature levels for -fl for maximimum size
    {
        { L"9.1",  2048 },
        { L"9.2",  2048 },
        { L"9.3",  4096 },
        { L"10.0", 8192 },
        { L"10.1", 8192 },
        { L"11.0", 16384 },
        { L"11.1", 16384 },
        { L"12.0", 16384 },
        { L"12.1", 16384 },
        { nullptr, 0 },
    };
}
namespace texdiag
{
    enum COMMANDS
    {
        CMD_INFO = 1,
        CMD_ANALYZE,
        CMD_COMPARE,
        CMD_DIFF,
        CMD_DUMPBC,
        CMD_DUMPDDS,
        CMD_MAX
    };

    enum OPTIONS
    {
        OPT_RECURSIVE = 1,
        OPT_FORMAT,
        OPT_FILTER,
        OPT_DDS_DWORD_ALIGN,
        OPT_DDS_BAD_DXTN_TAILS,
        OPT_OUTPUTFILE,
        OPT_TOLOWER,
        OPT_OVERWRITE,
        OPT_FILETYPE,
        OPT_NOLOGO,
        OPT_TYPELESS_UNORM,
        OPT_TYPELESS_FLOAT,
        OPT_EXPAND_LUMINANCE,
        OPT_TARGET_PIXELX,
        OPT_TARGET_PIXELY,
        OPT_FILELIST,
        OPT_MAX
    };

    inline HANDLE safe_handle(HANDLE h) noexcept { return (h == INVALID_HANDLE_VALUE) ? nullptr : h; }

    struct find_closer { void operator()(HANDLE h) noexcept { assert(h != INVALID_HANDLE_VALUE); if (h) FindClose(h); } };

    using ScopedFindHandle = std::unique_ptr<void, find_closer>;

#ifdef _PREFAST_
#pragma prefast(disable : 26018, "Only used with static internal arrays")
#endif

    DWORD LookupByName(const wchar_t* pName, const SValue* pArray)
    {
        while (pArray->pName)
        {
            if (!_wcsicmp(pName, pArray->pName))
                return pArray->dwValue;

            pArray++;
        }

        return 0;
    }


    const wchar_t* LookupByValue(DWORD pValue, const SValue* pArray)
    {
        while (pArray->pName)
        {
            if (pValue == pArray->dwValue)
                return pArray->pName;

            pArray++;
        }

        return L"";
    }


    void SearchForFiles(const wchar_t* path, std::list<SConversion>& files, bool recursive)
    {
        // Process files
        WIN32_FIND_DATAW findData = {};
        ScopedFindHandle hFile(safe_handle(FindFirstFileExW(path,
            FindExInfoBasic, &findData,
            FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH)));
        if (hFile)
        {
            for (;;)
            {
                if (!(findData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)))
                {
                    wchar_t drive[_MAX_DRIVE] = {};
                    wchar_t dir[_MAX_DIR] = {};
                    _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

                    SConversion conv = {};
                    _wmakepath_s(conv.szSrc, drive, dir, findData.cFileName, nullptr);
                    files.push_back(conv);
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }

        // Process directories
        if (recursive)
        {
            wchar_t searchDir[MAX_PATH] = {};
            {
                wchar_t drive[_MAX_DRIVE] = {};
                wchar_t dir[_MAX_DIR] = {};
                _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
                _wmakepath_s(searchDir, drive, dir, L"*", nullptr);
            }

            hFile.reset(safe_handle(FindFirstFileExW(searchDir,
                FindExInfoBasic, &findData,
                FindExSearchLimitToDirectories, nullptr,
                FIND_FIRST_EX_LARGE_FETCH)));
            if (!hFile)
                return;

            for (;;)
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (findData.cFileName[0] != L'.')
                    {
                        wchar_t subdir[MAX_PATH] = {};

                        {
                            wchar_t drive[_MAX_DRIVE] = {};
                            wchar_t dir[_MAX_DIR] = {};
                            wchar_t fname[_MAX_FNAME] = {};
                            wchar_t ext[_MAX_FNAME] = {};
                            _wsplitpath_s(path, drive, dir, fname, ext);
                            wcscat_s(dir, findData.cFileName);
                            _wmakepath_s(subdir, drive, dir, fname, ext);
                        }

                        SearchForFiles(subdir, files, recursive);
                    }
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }
    }


    void PrintFormat(DXGI_FORMAT Format)
    {
        for (const SValue* pFormat = g_pFormats; pFormat->pName; pFormat++)
        {
            if (static_cast<DXGI_FORMAT>(pFormat->dwValue) == Format)
            {
                wprintf(L"%ls", pFormat->pName);
                return;
            }
        }

        for (const SValue* pFormat = g_pReadOnlyFormats; pFormat->pName; pFormat++)
        {
            if (static_cast<DXGI_FORMAT>(pFormat->dwValue) == Format)
            {
                wprintf(L"%ls", pFormat->pName);
                return;
            }
        }

        wprintf(L"*UNKNOWN*");
    }


    void PrintList(size_t cch, const SValue* pValue)
    {
        while (pValue->pName)
        {
            size_t cchName = wcslen(pValue->pName);

            if (cch + cchName + 2 >= 80)
            {
                wprintf(L"\n      ");
                cch = 6;
            }

            wprintf(L"%ls ", pValue->pName);
            cch += cchName + 2;
            pValue++;
        }

        wprintf(L"\n");
    }


    void PrintLogo()
    {
        wchar_t version[32] = {};

        wchar_t appName[_MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, appName, static_cast<DWORD>(std::size(appName))))
        {
            DWORD size = GetFileVersionInfoSizeW(appName, nullptr);
            if (size > 0)
            {
                auto verInfo = std::make_unique<uint8_t[]>(size);
                if (GetFileVersionInfoW(appName, 0, size, verInfo.get()))
                {
                    LPVOID lpstr = nullptr;
                    UINT strLen = 0;
                    if (VerQueryValueW(verInfo.get(), L"\\StringFileInfo\\040904B0\\ProductVersion", &lpstr, &strLen))
                    {
                        wcsncpy_s(version, reinterpret_cast<const wchar_t*>(lpstr), strLen);
                    }
                }
            }
        }

        if (!*version || wcscmp(version, L"1.0.0.0") == 0)
        {
            swprintf_s(version, L"%03d (library)", DIRECTX_TEX_VERSION);
        }

        wprintf(L"Microsoft (R) DirectX Texture Diagnostic Tool [DirectXTex] Version %ls\n", version);
        wprintf(L"Copyright (C) Microsoft Corp. All rights reserved.\n");
#ifdef _DEBUG
        wprintf(L"*** Debug build ***\n");
#endif
        wprintf(L"\n");
    }




    HRESULT LoadImage(
        const wchar_t* fileName,
        DWORD dwOptions,
        TEX_FILTER_FLAGS dwFilter,
        TexMetadata& info,
        std::unique_ptr<ScratchImage>& image)
    {
        if (!fileName)
            return E_INVALIDARG;

        image.reset(new (std::nothrow) ScratchImage);
        if (!image)
            return E_OUTOFMEMORY;

        wchar_t ext[_MAX_EXT] = {};
        _wsplitpath_s(fileName, nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT);

        if (_wcsicmp(ext, L".dds") == 0)
        {
            DDS_FLAGS ddsFlags = DDS_FLAGS_ALLOW_LARGE_FILES;
            if (dwOptions & (1 << OPT_DDS_DWORD_ALIGN))
                ddsFlags |= DDS_FLAGS_LEGACY_DWORD;
            if (dwOptions & (1 << OPT_EXPAND_LUMINANCE))
                ddsFlags |= DDS_FLAGS_EXPAND_LUMINANCE;
            if (dwOptions & (1 << OPT_DDS_BAD_DXTN_TAILS))
                ddsFlags |= DDS_FLAGS_BAD_DXTN_TAILS;

            HRESULT hr = LoadFromDDSFile(fileName, ddsFlags, &info, *image);
            if (FAILED(hr))
                return hr;

            if (IsTypeless(info.format))
            {
                if (dwOptions & (1 << OPT_TYPELESS_UNORM))
                {
                    info.format = MakeTypelessUNORM(info.format);
                }
                else if (dwOptions & (1 << OPT_TYPELESS_FLOAT))
                {
                    info.format = MakeTypelessFLOAT(info.format);
                }

                if (IsTypeless(info.format))
                    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

                image->OverrideFormat(info.format);
            }

            return S_OK;
        }
        else if (_wcsicmp(ext, L".tga") == 0)
        {
            return LoadFromTGAFile(fileName, TGA_FLAGS_NONE, &info, *image);
        }
        else if (_wcsicmp(ext, L".hdr") == 0)
        {
            return LoadFromHDRFile(fileName, &info, *image);
        }
#ifdef USE_OPENEXR
        else if (_wcsicmp(ext, L".exr") == 0)
        {
            return LoadFromEXRFile(fileName, &info, *image);
        }
#endif
        else
        {
            // WIC shares the same filter values for mode and dither
            static_assert(static_cast<int>(WIC_FLAGS_DITHER) == static_cast<int>(TEX_FILTER_DITHER), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_DITHER_DIFFUSION) == static_cast<int>(TEX_FILTER_DITHER_DIFFUSION), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_POINT) == static_cast<int>(TEX_FILTER_POINT), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_LINEAR) == static_cast<int>(TEX_FILTER_LINEAR), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_CUBIC) == static_cast<int>(TEX_FILTER_CUBIC), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_FANT) == static_cast<int>(TEX_FILTER_FANT), "WIC_FLAGS_* & TEX_FILTER_* should match");

            return LoadFromWICFile(fileName, dwFilter | WIC_FLAGS_ALL_FRAMES, &info, *image);
        }
    }



    //--------------------------------------------------------------------------------------
    struct AnalyzeData
    {
        XMFLOAT4 imageMin;
        XMFLOAT4 imageMax;
        XMFLOAT4 imageAvg;
        XMFLOAT4 imageVariance;
        XMFLOAT4 imageStdDev;
        float luminance;
        size_t   specials_x;
        size_t   specials_y;
        size_t   specials_z;
        size_t   specials_w;

        void Print()
        {
            wprintf(L"\t  Minimum - (%f %f %f %f)\n", imageMin.x, imageMin.y, imageMin.z, imageMin.w);
            wprintf(L"\t  Average - (%f %f %f %f)\n", imageAvg.x, imageAvg.y, imageAvg.z, imageAvg.w);
            wprintf(L"\t  Maximum - (%f %f %f %f)\n", imageMax.x, imageMax.y, imageMax.z, imageMax.w);
            wprintf(L"\t Variance - (%f %f %f %f)\n", imageVariance.x, imageVariance.y, imageVariance.z, imageVariance.w);
            wprintf(L"\t  Std Dev - (%f %f %f %f)\n", imageStdDev.x, imageStdDev.y, imageStdDev.z, imageStdDev.w);

            wprintf(L"\tLuminance - %f (maximum)\n", luminance);

            if ((specials_x > 0) || (specials_y > 0) || (specials_z > 0) || (specials_w > 0))
            {
                wprintf(L"     FP specials - (%zu %zu %zu %zu)\n", specials_x, specials_y, specials_z, specials_w);
            }
        }
    };

    HRESULT Analyze(const Image& image, _Out_ AnalyzeData& result)
    {
        memset(&result, 0, sizeof(AnalyzeData));

        // First pass
        XMVECTOR minv = g_XMFltMax;
        XMVECTOR maxv = XMVectorNegate(g_XMFltMax);
        XMVECTOR acc = g_XMZero;
        XMVECTOR luminance = g_XMZero;

        size_t totalPixels = 0;

        HRESULT hr = EvaluateImage(image, [&](const XMVECTOR* pixels, size_t width, size_t y)
            {
                static const XMVECTORF32 s_luminance = { { {  0.3f, 0.59f, 0.11f, 0.f } } };

                UNREFERENCED_PARAMETER(y);

                for (size_t x = 0; x < width; ++x)
                {
                    XMVECTOR v = *pixels++;
                    luminance = XMVectorMax(luminance, XMVector3Dot(v, s_luminance));
                    minv = XMVectorMin(minv, v);
                    maxv = XMVectorMax(maxv, v);
                    acc = XMVectorAdd(v, acc);
                    ++totalPixels;

                    XMFLOAT4 f;
                    XMStoreFloat4(&f, v);
                    if (!isfinite(f.x))
                    {
                        ++result.specials_x;
                    }

                    if (!isfinite(f.y))
                    {
                        ++result.specials_y;
                    }

                    if (!isfinite(f.z))
                    {
                        ++result.specials_z;
                    }

                    if (!isfinite(f.w))
                    {
                        ++result.specials_w;
                    }
                }
            });
        if (FAILED(hr))
            return hr;

        if (!totalPixels)
            return S_FALSE;

        result.luminance = XMVectorGetX(luminance);
        XMStoreFloat4(&result.imageMin, minv);
        XMStoreFloat4(&result.imageMax, maxv);

        XMVECTOR pixelv = XMVectorReplicate(float(totalPixels));
        XMVECTOR avgv = XMVectorDivide(acc, pixelv);
        XMStoreFloat4(&result.imageAvg, avgv);

        // Second pass
        acc = g_XMZero;

        hr = EvaluateImage(image, [&](const XMVECTOR* pixels, size_t width, size_t y)
            {
                UNREFERENCED_PARAMETER(y);

                for (size_t x = 0; x < width; ++x)
                {
                    XMVECTOR v = *pixels++;

                    XMVECTOR diff = XMVectorSubtract(v, avgv);
                    acc = XMVectorMultiplyAdd(diff, diff, acc);
                }
            });
        if (FAILED(hr))
            return hr;

        XMStoreFloat4(&result.imageVariance, acc);

        XMVECTOR stddev = XMVectorSqrt(acc);

        XMStoreFloat4(&result.imageStdDev, stddev);

        return S_OK;
    }


    //--------------------------------------------------------------------------------------
    struct AnalyzeBCData
    {
        size_t blocks;
        size_t blockHist[15];

        void Print(DXGI_FORMAT fmt)
        {
            wprintf(L"\t        Compression - ");
            PrintFormat(fmt);
            wprintf(L"\n\t       Total blocks - %zu\n", blocks);

            switch (fmt)
            {
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                wprintf(L"\t     4 color blocks - %zu\n", blockHist[0]);
                wprintf(L"\t     3 color blocks - %zu\n", blockHist[1]);
                break;

                // BC2 only has a single 'type' of block

            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
                wprintf(L"\t     8 alpha blocks - %zu\n", blockHist[0]);
                wprintf(L"\t     6 alpha blocks - %zu\n", blockHist[1]);
                break;

            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
                wprintf(L"\t     8 red blocks - %zu\n", blockHist[0]);
                wprintf(L"\t     6 red blocks - %zu\n", blockHist[1]);
                break;

            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
                wprintf(L"\t     8 red blocks - %zu\n", blockHist[0]);
                wprintf(L"\t     6 red blocks - %zu\n", blockHist[1]);
                wprintf(L"\t   8 green blocks - %zu\n", blockHist[2]);
                wprintf(L"\t   6 green blocks - %zu\n", blockHist[3]);
                break;

            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
                for (size_t j = 1; j <= 14; ++j)
                {
                    if (blockHist[j] > 0)
                        wprintf(L"\t     Mode %02zu blocks - %zu\n", j, blockHist[j]);
                }
                if (blockHist[0] > 0)
                    wprintf(L"\tReserved mode blcks - %zu\n", blockHist[0]);
                break;

            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                for (size_t j = 0; j <= 7; ++j)
                {
                    if (blockHist[j] > 0)
                        wprintf(L"\t     Mode %02zu blocks - %zu\n", j, blockHist[j]);
                }
                if (blockHist[8] > 0)
                    wprintf(L"\tReserved mode blcks - %zu\n", blockHist[8]);
                break;

            default:
                break;
            }
        }
    };

#pragma pack(push,1)
    struct BC1Block
    {
        uint16_t    rgb[2]; // 565 colors
        uint32_t    bitmap; // 2bpp rgb bitmap
    };

    struct BC2Block
    {
        uint32_t    bitmap[2];  // 4bpp alpha bitmap
        BC1Block    bc1;        // BC1 rgb data
    };

    struct BC3Block
    {
        uint8_t     alpha[2];   // alpha values
        uint8_t     bitmap[6];  // 3bpp alpha bitmap
        BC1Block    bc1;        // BC1 rgb data
    };

    struct BC4UBlock
    {
        uint8_t red_0;
        uint8_t red_1;
        uint8_t indices[6];
    };

    struct BC4SBlock
    {
        int8_t red_0;
        int8_t red_1;
        uint8_t indices[6];
    };

    struct BC5UBlock
    {
        BC4UBlock u;
        BC4UBlock v;
    };

    struct BC5SBlock
    {
        BC4SBlock u;
        BC4SBlock v;
    };
#pragma pack(pop)

    HRESULT AnalyzeBC(const Image& image, _Out_ AnalyzeBCData& result)
    {
        memset(&result, 0, sizeof(AnalyzeBCData));

        size_t sbpp;
        switch (image.format)
        {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            sbpp = 8;
            break;

        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            sbpp = 16;
            break;

        default:
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        const uint8_t* pSrc = image.pixels;
        const size_t rowPitch = image.rowPitch;

        for (size_t h = 0; h < image.height; h += 4)
        {
            const uint8_t* sptr = pSrc;

            for (size_t count = 0; count < rowPitch; count += sbpp)
            {
                switch (image.format)
                {
                case DXGI_FORMAT_BC1_UNORM:
                case DXGI_FORMAT_BC1_UNORM_SRGB:
                {
                    auto block = reinterpret_cast<const BC1Block*>(sptr);

                    if (block->rgb[0] <= block->rgb[1])
                    {
                        // Transparent block
                        ++result.blockHist[1];
                    }
                    else
                    {
                        // Opaque block
                        ++result.blockHist[0];
                    }
                }
                break;

                // BC2 only has a single 'type' of block

                case DXGI_FORMAT_BC3_UNORM:
                case DXGI_FORMAT_BC3_UNORM_SRGB:
                {
                    auto block = reinterpret_cast<const BC3Block*>(sptr);

                    if (block->alpha[0] > block->alpha[1])
                    {
                        // 8 alpha block
                        ++result.blockHist[0];
                    }
                    else
                    {
                        // 6 alpha block
                        ++result.blockHist[1];
                    }
                }
                break;

                case DXGI_FORMAT_BC4_UNORM:
                {
                    auto block = reinterpret_cast<const BC4UBlock*>(sptr);

                    if (block->red_0 > block->red_1)
                    {
                        // 8 red block
                        ++result.blockHist[0];
                    }
                    else
                    {
                        // 6 red block
                        ++result.blockHist[1];
                    }
                }
                break;

                case DXGI_FORMAT_BC4_SNORM:
                {
                    auto block = reinterpret_cast<const BC4SBlock*>(sptr);

                    if (block->red_0 > block->red_1)
                    {
                        // 8 red block
                        ++result.blockHist[0];
                    }
                    else
                    {
                        // 6 red block
                        ++result.blockHist[1];
                    }
                }
                break;

                case DXGI_FORMAT_BC5_UNORM:
                {
                    auto block = reinterpret_cast<const BC5UBlock*>(sptr);

                    if (block->u.red_0 > block->u.red_1)
                    {
                        // 8 red block
                        ++result.blockHist[0];
                    }
                    else
                    {
                        // 6 red block
                        ++result.blockHist[1];
                    }

                    if (block->v.red_0 > block->v.red_1)
                    {
                        // 8 green block
                        ++result.blockHist[2];
                    }
                    else
                    {
                        // 6 green block
                        ++result.blockHist[3];
                    }
                }
                break;

                case DXGI_FORMAT_BC5_SNORM:
                {
                    auto block = reinterpret_cast<const BC5SBlock*>(sptr);

                    if (block->u.red_0 > block->u.red_1)
                    {
                        // 8 red block
                        ++result.blockHist[0];
                    }
                    else
                    {
                        // 6 red block
                        ++result.blockHist[1];
                    }

                    if (block->v.red_0 > block->v.red_1)
                    {
                        // 8 green block
                        ++result.blockHist[2];
                    }
                    else
                    {
                        // 6 green block
                        ++result.blockHist[3];
                    }
                }
                break;

                case DXGI_FORMAT_BC6H_UF16:
                case DXGI_FORMAT_BC6H_SF16:
                    switch (*sptr & 0x03)
                    {
                    case 0x00:
                        // Mode 1 (2 bits, 00)
                        ++result.blockHist[1];
                        break;

                    case 0x01:
                        // Mode 2 (2 bits, 01)
                        ++result.blockHist[2];
                        break;

                    default:
                        switch (*sptr & 0x1F)
                        {
                        case 0x02:
                            // Mode 3 (5 bits, 00010)
                            ++result.blockHist[3];
                            break;

                        case 0x06:
                            // Mode 4 (5 bits, 00110)
                            ++result.blockHist[4];
                            break;

                        case 0x0A:
                            // Mode 5 (5 bits, 01010)
                            ++result.blockHist[5];
                            break;

                        case 0x0E:
                            // Mode 6 (5 bits, 01110)
                            ++result.blockHist[6];
                            break;

                        case 0x12:
                            // Mode 7 (5 bits, 10010)
                            ++result.blockHist[7];
                            break;

                        case 0x16:
                            // Mode 8 (5 bits, 10110)
                            ++result.blockHist[8];
                            break;

                        case 0x1A:
                            // Mode 9 (5 bits, 11010)
                            ++result.blockHist[9];
                            break;

                        case 0x1E:
                            // Mode 10 (5 bits, 11110)
                            ++result.blockHist[10];
                            break;

                        case 0x03:
                            // Mode 11 (5 bits, 00011)
                            ++result.blockHist[11];
                            break;

                        case 0x07:
                            // Mode 12 (5 bits, 00111)
                            ++result.blockHist[12];
                            break;

                        case 0x0B:
                            // Mode 13 (5 bits, 01011)
                            ++result.blockHist[13];
                            break;

                        case 0x0F:
                            // Mode 14 (5 bits, 01111)
                            ++result.blockHist[14];
                            break;

                        case 0x13: // Reserved mode (5 bits, 10011)
                        case 0x17: // Reserved mode (5 bits, 10111)
                        case 0x1B: // Reserved mode (5 bits, 11011)
                        case 0x1F: // Reserved mode (5 bits, 11111)
                        default:
                            ++result.blockHist[0];
                            break;
                        }
                        break;
                    }
                    break;

                case DXGI_FORMAT_BC7_UNORM:
                case DXGI_FORMAT_BC7_UNORM_SRGB:
                    if (*sptr & 0x01)
                    {
                        // Mode 0 (1)
                        ++result.blockHist[0];
                    }
                    else if (*sptr & 0x02)
                    {
                        // Mode 1 (01)
                        ++result.blockHist[1];
                    }
                    else if (*sptr & 0x04)
                    {
                        // Mode 2 (001)
                        ++result.blockHist[2];
                    }
                    else if (*sptr & 0x08)
                    {
                        // Mode 3 (0001)
                        ++result.blockHist[3];
                    }
                    else if (*sptr & 0x10)
                    {
                        // Mode 4 (00001)
                        ++result.blockHist[4];
                    }
                    else if (*sptr & 0x20)
                    {
                        // Mode 5 (000001)
                        ++result.blockHist[5];
                    }
                    else if (*sptr & 0x40)
                    {
                        // Mode 6 (0000001)
                        ++result.blockHist[6];
                    }
                    else if (*sptr & 0x80)
                    {
                        // Mode 7 (00000001)
                        ++result.blockHist[7];
                    }
                    else
                    {
                        // Reserved mode 8 (00000000)
                        ++result.blockHist[8];
                    }
                    break;

                default:
                    break;
                }

                sptr += sbpp;
                ++result.blocks;
            }

            pSrc += rowPitch;
        }

        return S_OK;
    }


    //--------------------------------------------------------------------------------------
    HRESULT Difference(
        const Image& image1,
        const Image& image2,
        TEX_FILTER_FLAGS dwFilter,
        DXGI_FORMAT format,
        ScratchImage& result)
    {
        if (!image1.pixels || !image2.pixels)
            return E_POINTER;

        if (image1.width != image2.width
            || image1.height != image2.height)
            return E_FAIL;

        ScratchImage tempA;
        const Image* imageA = &image1;
        if (IsCompressed(image1.format))
        {
            HRESULT hr = Decompress(image1, DXGI_FORMAT_R32G32B32A32_FLOAT, tempA);
            if (FAILED(hr))
                return hr;

            imageA = tempA.GetImage(0, 0, 0);
        }

        ScratchImage tempB;
        const Image* imageB = &image2;
        if (image2.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            if (IsCompressed(image2.format))
            {
                HRESULT hr = Decompress(image2, DXGI_FORMAT_R32G32B32A32_FLOAT, tempB);
                if (FAILED(hr))
                    return hr;

                imageB = tempB.GetImage(0, 0, 0);
            }
            else
            {
                HRESULT hr = Convert(image2, DXGI_FORMAT_R32G32B32A32_FLOAT, dwFilter, TEX_THRESHOLD_DEFAULT, tempB);
                if (FAILED(hr))
                    return hr;

                imageB = tempB.GetImage(0, 0, 0);
            }
        }

        if (!imageA || !imageB)
            return E_POINTER;

        ScratchImage diffImage;
        HRESULT hr = TransformImage(*imageA, [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t y)
            {
                auto* inPixelsB = reinterpret_cast<XMVECTOR*>(imageB->pixels + (y * imageB->rowPitch));

                for (size_t x = 0; x < width; ++x)
                {
                    XMVECTOR v1 = *inPixels++;
                    XMVECTOR v2 = *inPixelsB++;

                    v1 = XMVectorSubtract(v1, v2);
                    v1 = XMVectorAbs(v1);

                    v1 = XMVectorSelect(g_XMIdentityR3, v1, g_XMSelect1110);

                    *outPixels++ = v1;
                }
            }, (format == DXGI_FORMAT_R32G32B32A32_FLOAT) ? result : diffImage);
        if (FAILED(hr))
            return hr;

        if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
            return S_OK;

        return Convert(diffImage.GetImages(), diffImage.GetImageCount(), diffImage.GetMetadata(), format, dwFilter, TEX_THRESHOLD_DEFAULT, result);
    }


    //--------------------------------------------------------------------------------------
    // Partition, Shape, Fixup
    const uint8_t g_aFixUp[3][64][3] =
    {
        {   // No fix-ups for 1st subset for BC6H or BC7
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },
            { 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 },{ 0, 0, 0 }
        },

        {   // BC6H/BC7 Partition Set Fixups for 2 Subsets
            { 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0,15, 0 },{ 0, 2, 0 },{ 0, 8, 0 },{ 0, 2, 0 },
            { 0, 2, 0 },{ 0, 8, 0 },{ 0, 8, 0 },{ 0,15, 0 },
            { 0, 2, 0 },{ 0, 8, 0 },{ 0, 2, 0 },{ 0, 2, 0 },
            { 0, 8, 0 },{ 0, 8, 0 },{ 0, 2, 0 },{ 0, 2, 0 },

            // BC7 Partition Set Fixups for 2 Subsets (second-half)
            { 0,15, 0 },{ 0,15, 0 },{ 0, 6, 0 },{ 0, 8, 0 },
            { 0, 2, 0 },{ 0, 8, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0, 2, 0 },{ 0, 8, 0 },{ 0, 2, 0 },{ 0, 2, 0 },
            { 0, 2, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0, 6, 0 },
            { 0, 6, 0 },{ 0, 2, 0 },{ 0, 6, 0 },{ 0, 8, 0 },
            { 0,15, 0 },{ 0,15, 0 },{ 0, 2, 0 },{ 0, 2, 0 },
            { 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },{ 0,15, 0 },
            { 0,15, 0 },{ 0, 2, 0 },{ 0, 2, 0 },{ 0,15, 0 }
        },

        {   // BC7 Partition Set Fixups for 3 Subsets
            { 0, 3,15 },{ 0, 3, 8 },{ 0,15, 8 },{ 0,15, 3 },
            { 0, 8,15 },{ 0, 3,15 },{ 0,15, 3 },{ 0,15, 8 },
            { 0, 8,15 },{ 0, 8,15 },{ 0, 6,15 },{ 0, 6,15 },
            { 0, 6,15 },{ 0, 5,15 },{ 0, 3,15 },{ 0, 3, 8 },
            { 0, 3,15 },{ 0, 3, 8 },{ 0, 8,15 },{ 0,15, 3 },
            { 0, 3,15 },{ 0, 3, 8 },{ 0, 6,15 },{ 0,10, 8 },
            { 0, 5, 3 },{ 0, 8,15 },{ 0, 8, 6 },{ 0, 6,10 },
            { 0, 8,15 },{ 0, 5,15 },{ 0,15,10 },{ 0,15, 8 },
            { 0, 8,15 },{ 0,15, 3 },{ 0, 3,15 },{ 0, 5,10 },
            { 0, 6,10 },{ 0,10, 8 },{ 0, 8, 9 },{ 0,15,10 },
            { 0,15, 6 },{ 0, 3,15 },{ 0,15, 8 },{ 0, 5,15 },
            { 0,15, 3 },{ 0,15, 6 },{ 0,15, 6 },{ 0,15, 8 },
            { 0, 3,15 },{ 0,15, 3 },{ 0, 5,15 },{ 0, 5,15 },
            { 0, 5,15 },{ 0, 8,15 },{ 0, 5,15 },{ 0,10,15 },
            { 0, 5,15 },{ 0,10,15 },{ 0, 8,15 },{ 0,13,15 },
            { 0,15, 3 },{ 0,12,15 },{ 0, 3,15 },{ 0, 3, 8 }
        }
    };

    inline static bool IsFixUpOffset(
        _In_range_(0, 2) size_t uPartitions,
        _In_range_(0, 63) uint64_t uShape,
        _In_range_(0, 15) size_t uOffset)
    {
        for (size_t p = 0; p <= uPartitions; p++)
        {
            if (uOffset == g_aFixUp[uPartitions][uShape][p])
            {
                return true;
            }
        }
        return false;
    }

    //--------------------------------------------------------------------------------------
#define SIGN_EXTEND(x,nb) ((((x)&(1<<((nb)-1)))?((~0)^((1<<(nb))-1)):0)|(x))

#define NUM_PIXELS_PER_BLOCK 16

    void Print565(uint16_t rgb)
    {
        auto r = float(((rgb >> 11) & 31) * (1.0f / 31.0f));
        auto g = float(((rgb >> 5) & 63) * (1.0f / 63.0f));
        auto b = float(((rgb >> 0) & 31) * (1.0f / 31.0f));

        wprintf(L"(R: %.3f, G: %.3f, B: %.3f)", r, g, b);
    }

    void PrintIndex2bpp(uint32_t bitmap)
    {
        for (size_t j = 0; j < NUM_PIXELS_PER_BLOCK; ++j, bitmap >>= 2)
        {
            wprintf(L"%u%ls", bitmap & 0x3, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
        }
    }

    void PrintIndex2bpp(uint64_t bitmap, size_t parts, uint64_t shape)
    {
        for (size_t j = 0; j < NUM_PIXELS_PER_BLOCK; ++j)
        {
            if (IsFixUpOffset(parts, shape, j))
            {
                wprintf(L"%llu%ls", bitmap & 0x1, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 1;
            }
            else
            {
                wprintf(L"%llu%ls", bitmap & 0x3, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 2;
            }
        }
    }

    void PrintIndex3bpp(uint64_t bitmap, size_t parts, uint64_t shape)
    {
        for (size_t j = 0; j < NUM_PIXELS_PER_BLOCK; ++j)
        {
            if (IsFixUpOffset(parts, shape, j))
            {
                wprintf(L"%llu%ls", bitmap & 0x3, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 2;
            }
            else
            {
                wprintf(L"%llu%ls", bitmap & 0x7, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 3;
            }
        }
    }

    void PrintIndex4bpp(uint64_t bitmap, size_t parts, uint64_t shape)
    {
        for (size_t j = 0; j < NUM_PIXELS_PER_BLOCK; ++j)
        {
            if (IsFixUpOffset(parts, shape, j))
            {
                wprintf(L"%llX%ls", bitmap & 0x7, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 3;
            }
            else
            {
                wprintf(L"%llX%ls", bitmap & 0xF, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                bitmap >>= 4;
            }
        }
    }

    void PrintIndex3bpp(const uint8_t data[6])
    {
        uint32_t bitmap = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);

        size_t j = 0;
        for (; j < (NUM_PIXELS_PER_BLOCK / 2); ++j, bitmap >>= 3)
        {
            wprintf(L"%u%ls", bitmap & 0x7, ((j % 4) == 3) ? L" | " : L" ");
        }

        bitmap = uint32_t(data[3]) | (uint32_t(data[4]) << 8) | (uint32_t(data[5]) << 16);

        for (; j < NUM_PIXELS_PER_BLOCK; ++j, bitmap >>= 3)
        {
            wprintf(L"%u%ls", bitmap & 0x7, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
        }
    }

    const wchar_t* GetRotBits(uint64_t rot)
    {
        switch (rot)
        {
        case 1: return L" (R<->A)";
        case 2: return L" (G<->A)";
        case 3: return L" (B<->A)";
        default: return L"";
        }
    }

    HRESULT DumpBCImage(const Image& image, int pixelx, int pixely)
    {
        size_t sbpp;
        switch (image.format)
        {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            sbpp = 8;
            break;

        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            sbpp = 16;
            break;

        default:
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        const uint8_t* pSrc = image.pixels;
        const size_t rowPitch = image.rowPitch;

        size_t nblock = 0;
        for (size_t h = 0; h < image.height; h += 4, pSrc += rowPitch)
        {
            if (pixely >= 0)
            {
                if ((pixely < int(h)) || (pixely >= int(h + 4)))
                    continue;
            }

            const uint8_t* sptr = pSrc;

            size_t w = 0;
            for (size_t count = 0; count < rowPitch; count += sbpp, w += 4, ++nblock, sptr += sbpp)
            {
                if (pixelx >= 0)
                {
                    if ((pixelx < int(w)) || (pixelx >= int(w + 4)))
                        continue;
                }

                wprintf(L"   Block %zu (pixel: %zu x %zu)\n", nblock, w, h);
                switch (image.format)
                {
                case DXGI_FORMAT_BC1_UNORM:
                case DXGI_FORMAT_BC1_UNORM_SRGB:
                {
                    auto block = reinterpret_cast<const BC1Block*>(sptr);

                    if (block->rgb[0] <= block->rgb[1])
                    {
                        // Transparent block
                        wprintf(L"\tTransparent - E0: ");
                    }
                    else
                    {
                        // Opaque block
                        wprintf(L"\t     Opaque - E0: ");
                    }

                    Print565(block->rgb[0]);
                    wprintf(L"\n\t              E1: ");
                    Print565(block->rgb[1]);
                    wprintf(L"\n\t           Index: ");
                    PrintIndex2bpp(block->bitmap);
                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC2_UNORM:
                case DXGI_FORMAT_BC2_UNORM_SRGB:
                {
                    auto block = reinterpret_cast<const BC2Block*>(sptr);

                    wprintf(L"\tColor - E0: ");
                    Print565(block->bc1.rgb[0]);
                    wprintf(L"\n\t        E1: ");
                    Print565(block->bc1.rgb[1]);
                    wprintf(L"\n\t     Index: ");
                    PrintIndex2bpp(block->bc1.bitmap);
                    wprintf(L"\n");

                    wprintf(L"\tAlpha - ");

                    size_t j = 0;
                    uint32_t bitmap = block->bitmap[0];
                    for (; j < (NUM_PIXELS_PER_BLOCK / 2); ++j, bitmap >>= 4)
                    {
                        wprintf(L"%X%ls", bitmap & 0xF, ((j % 4) == 3) ? L" | " : L" ");
                    }

                    bitmap = block->bitmap[1];
                    for (; j < NUM_PIXELS_PER_BLOCK; ++j, bitmap >>= 4)
                    {
                        wprintf(L"%X%ls", bitmap & 0xF, ((j < (NUM_PIXELS_PER_BLOCK - 1)) && ((j % 4) == 3)) ? L" | " : L" ");
                    }

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC3_UNORM:
                case DXGI_FORMAT_BC3_UNORM_SRGB:
                {
                    auto block = reinterpret_cast<const BC3Block*>(sptr);

                    wprintf(L"\tColor - E0: ");
                    Print565(block->bc1.rgb[0]);
                    wprintf(L"\n\t        E1: ");
                    Print565(block->bc1.rgb[1]);
                    wprintf(L"\n\t     Index: ");
                    PrintIndex2bpp(block->bc1.bitmap);
                    wprintf(L"\n");

                    wprintf(L"\tAlpha - E0: %0.3f  E1: %0.3f (%u)\n\t     Index: ",
                        (float(block->alpha[0]) / 255.f),
                        (float(block->alpha[1]) / 255.f), (block->alpha[0] > block->alpha[1]) ? 8u : 6u);

                    PrintIndex3bpp(block->bitmap);

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC4_UNORM:
                {
                    auto block = reinterpret_cast<const BC4UBlock*>(sptr);

                    wprintf(L"\t   E0: %0.3f  E1: %0.3f (%u)\n\tIndex: ",
                        (float(block->red_0) / 255.f),
                        (float(block->red_1) / 255.f), (block->red_0 > block->red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->indices);

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC4_SNORM:
                {
                    auto block = reinterpret_cast<const BC4SBlock*>(sptr);

                    wprintf(L"\t   E0: %0.3f  E1: %0.3f (%u)\n\tIndex: ",
                        (float(block->red_0) / 127.f),
                        (float(block->red_1) / 127.f), (block->red_0 > block->red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->indices);

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC5_UNORM:
                {
                    auto block = reinterpret_cast<const BC5UBlock*>(sptr);

                    wprintf(L"\tU -   E0: %0.3f  E1: %0.3f (%u)\n\t   Index: ",
                        (float(block->u.red_0) / 255.f),
                        (float(block->u.red_1) / 255.f), (block->u.red_0 > block->u.red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->u.indices);

                    wprintf(L"\n");

                    wprintf(L"\tV -   E0: %0.3f  E1: %0.3f (%u)\n\t   Index: ",
                        (float(block->v.red_0) / 255.f),
                        (float(block->v.red_1) / 255.f), (block->v.red_0 > block->v.red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->v.indices);

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC5_SNORM:
                {
                    auto block = reinterpret_cast<const BC5SBlock*>(sptr);

                    wprintf(L"\tU -   E0: %0.3f  E1: %0.3f (%u)\n\t   Index: ",
                        (float(block->u.red_0) / 127.f),
                        (float(block->u.red_1) / 127.f), (block->u.red_0 > block->u.red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->u.indices);

                    wprintf(L"\n");

                    wprintf(L"\tV -   E0: %0.3f  E1: %0.3f (%u)\n\t   Index: ",
                        (float(block->v.red_0) / 127.f),
                        (float(block->v.red_1) / 127.f), (block->v.red_0 > block->v.red_1) ? 8u : 6u);

                    PrintIndex3bpp(block->v.indices);

                    wprintf(L"\n");
                }
                break;

                case DXGI_FORMAT_BC6H_UF16:
                case DXGI_FORMAT_BC6H_SF16:
                    // http://msdn.microsoft.com/en-us/library/windows/desktop/hh308952.aspx#decoding_the_bc6h_format

                    switch (*sptr & 0x03)
                    {
                    case 0x00:
                        // Mode 1 (2 bits, 00)
                    {
                        struct bc6h_mode1
                        {
                            uint64_t mode : 2; // { M, 0}, { M, 1}
                            uint64_t gy4 : 1;  // {GY, 4}
                            uint64_t by4 : 1;  // {BY, 4}
                            uint64_t bz4 : 1;  // {BZ, 4}
                            uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                            uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                            uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                            uint64_t rx : 5;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}
                            uint64_t gz4 : 1;  // {GZ, 4}
                            uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                            uint64_t gx : 5;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}
                            uint64_t bz0 : 1;  // {BZ, 0},
                            uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                            uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}
                            uint64_t bz1 : 1;  // {BZ, 1}
                            uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                            uint64_t by3 : 1;  // {BY, 3}
                            uint64_t ry : 5;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}
                            uint64_t bz2 : 1;  // {BZ, 2}
                            uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}
                            uint64_t bz3 : 1;  // {BZ, 3}
                            uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                            uint64_t indices : 46;
                        };
                        static_assert(sizeof(bc6h_mode1) == 16, "Block size must be 16 bytes");

                        bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                        auto m = reinterpret_cast<const bc6h_mode1*>(sptr);

                        XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                        XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                        XMINT3 e1_A(int(m->ry),
                            int(m->gy | (m->gy4 << 4)),
                            int(m->by | (m->by3 << 3) | (m->by4 << 4)));
                        XMINT3 e1_B(int(m->rz),
                            int(m->gz | (m->gz4 << 4)),
                            int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4)));

                        if (bSigned)
                        {
                            e0_A.x = SIGN_EXTEND(e0_A.x, 10);
                            e0_A.y = SIGN_EXTEND(e0_A.y, 10);
                            e0_A.z = SIGN_EXTEND(e0_A.z, 10);

                            e0_B.x = SIGN_EXTEND(e0_B.x, 5);
                            e0_B.y = SIGN_EXTEND(e0_B.y, 5);
                            e0_B.z = SIGN_EXTEND(e0_B.z, 5);

                            e1_A.x = SIGN_EXTEND(e1_A.x, 5);
                            e1_A.y = SIGN_EXTEND(e1_A.y, 5);
                            e1_A.z = SIGN_EXTEND(e1_A.z, 5);

                            e1_B.x = SIGN_EXTEND(e1_B.x, 5);
                            e1_B.y = SIGN_EXTEND(e1_B.y, 5);
                            e1_B.z = SIGN_EXTEND(e1_B.z, 5);
                        }

                        wprintf(L"\tMode 1 - [10 5 5 5] shape %llu\n", m->d);
                        wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                        wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                        wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                        wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                        wprintf(L"\t         Index: ");
                        PrintIndex3bpp(m->indices, 1, m->d);
                        wprintf(L"\n");
                    }
                    break;

                    case 0x01:
                        // Mode 2 (2 bits, 01)
                    {
                        struct bc6h_mode2
                        {
                            uint64_t mode : 2; // { M, 0}, { M, 1}
                            uint64_t gy5 : 1;  // {GY, 5}
                            uint64_t gz45 : 2; // {GZ, 4}, {GZ, 5}
                            uint64_t rw : 7;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}
                            uint64_t bz : 2;   // {BZ, 0}, {BZ, 1}
                            uint64_t by4 : 1;  // {BY, 4},
                            uint64_t gw : 7;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}
                            uint64_t by5 : 1;  // {BY, 5}
                            uint64_t bz2 : 1;  // {BZ, 2}
                            uint64_t gy4 : 1;  // {GY, 4}
                            uint64_t bw : 7;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}
                            uint64_t bz3 : 1;  // {BZ, 3}
                            uint64_t bz5 : 1;  // {BZ, 5}
                            uint64_t bz4 : 1;  // {BZ, 4}
                            uint64_t rx : 6;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}
                            uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                            uint64_t gx : 6;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}
                            uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                            uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}
                            uint64_t by : 4;   // {BY, 0}, {BY, 1}, {BY, 2}, {BY, 3}
                            uint64_t ry : 6;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}, {RY, 5}
                            uint64_t rz : 6;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}, {RZ, 5},
                            uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                            uint64_t indices : 46;

                        };
                        static_assert(sizeof(bc6h_mode2) == 16, "Block size must be 16 bytes");

                        bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                        auto m = reinterpret_cast<const bc6h_mode2*>(sptr);

                        XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                        XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                        XMINT3 e1_A(int(m->ry),
                            int(m->gy | (m->gy4 << 4) | (m->gy5 << 5)),
                            int(m->by | (m->by4 << 4) | (m->by5 << 5)));
                        XMINT3 e1_B(int(m->rz),
                            int(m->gz | (m->gz45 << 4)),
                            int(m->bz | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4) | (m->bz5 << 5)));

                        if (bSigned)
                        {
                            e0_A.x = SIGN_EXTEND(e0_A.x, 7);
                            e0_A.y = SIGN_EXTEND(e0_A.y, 7);
                            e0_A.z = SIGN_EXTEND(e0_A.z, 7);

                            e0_B.x = SIGN_EXTEND(e0_B.x, 6);
                            e0_B.y = SIGN_EXTEND(e0_B.y, 6);
                            e0_B.z = SIGN_EXTEND(e0_B.z, 6);

                            e1_A.x = SIGN_EXTEND(e1_A.x, 6);
                            e1_A.y = SIGN_EXTEND(e1_A.y, 6);
                            e1_A.z = SIGN_EXTEND(e1_A.z, 6);

                            e1_B.x = SIGN_EXTEND(e1_B.x, 6);
                            e1_B.y = SIGN_EXTEND(e1_B.y, 6);
                            e1_B.z = SIGN_EXTEND(e1_B.z, 6);
                        }

                        wprintf(L"\tMode 2 - [7 6 6 6] shape %llu\n", m->d);
                        wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                        wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                        wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                        wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                        wprintf(L"\t         Index: ");
                        PrintIndex3bpp(m->indices, 1, m->d);
                        wprintf(L"\n");
                    }
                    break;

                    default:
                        switch (*sptr & 0x1F)
                        {
                        case 0x02:
                            // Mode 3 (5 bits, 00010)
                        {
                            struct bc6h_mode3
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 5;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 4;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 4;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 5;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;

                            };
                            static_assert(sizeof(bc6h_mode3) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode3*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10)),
                                int(m->gw | (m->gw10 << 10)),
                                int(m->bw | (m->bw10 << 10)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry), int(m->gy),
                                int(m->by | (m->by3 << 3)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 11);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 11);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 11);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 5);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 4);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 4);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 5);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 4);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 4);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 5);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 4);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 4);
                            }

                            wprintf(L"\tMode 3 - [11 5 4 4] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x06:
                            // Mode 4 (5 bits, 00110)
                        {
                            struct bc6h_mode4
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 4;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 5;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 4;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 4;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t rz : 4;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;

                            };
                            static_assert(sizeof(bc6h_mode4) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode4*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10)),
                                int(m->gw | (m->gw10 << 10)),
                                int(m->bw | (m->bw10 << 10)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4)),
                                int(m->by | (m->by3 << 3)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4)),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 11);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 11);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 11);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 4);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 5);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 4);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 4);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 5);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 4);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 4);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 5);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 4);
                            }

                            wprintf(L"\tMode 4 - [11 4 5 4] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x0A:
                            // Mode 5 (5 bits, 01010)
                        {
                            struct bc6h_mode5
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 4;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 4;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 4;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}
                                uint64_t bz12 : 2; // {BZ, 1}, {BZ, 2}
                                uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {BZ, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode5) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode5*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10)),
                                int(m->gw | (m->gw10 << 10)),
                                int(m->bw | (m->bw10 << 10)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry), int(m->gy),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4)));
                            XMINT3 e1_B(int(m->rz), int(m->gz),
                                int(m->bz0 | (m->bz12 << 1) | (m->bz3 << 3)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 11);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 11);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 11);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 4);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 4);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 5);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 4);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 4);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 5);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 4);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 4);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 5);
                            }

                            wprintf(L"\tMode 5 - [11 4 4 5] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x0E:
                            // Mode 6 (5 bits, 01110)
                        {
                            struct bc6h_mode6
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 9;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gw : 9;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bw : 9;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}
                                uint64_t bz4 : 1;  // {BZ, 4}
                                uint64_t rx : 5;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 5;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 5;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4},
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {BZ, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode6) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode6*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4)),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4)),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 9);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 9);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 9);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 5);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 5);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 5);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 5);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 5);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 5);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 5);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 5);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 5);
                            }

                            wprintf(L"\tMode 6 - [9 5 5 5] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x12:
                            // Mode 7 (5 bits, 10010)
                        {
                            struct bc6h_mode7
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 8;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gw : 8;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bw : 8;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t bz4 : 1;  // {BZ, 4}
                                uint64_t rx : 6;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 5;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 6;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}, {RY, 5}
                                uint64_t rz : 6;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}, {RZ, 5}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode7) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode7*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4)),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4)),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 8);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 8);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 8);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 6);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 5);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 5);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 6);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 5);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 5);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 6);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 5);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 5);
                            }

                            wprintf(L"\tMode 7 - [8 6 5 5] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x16:
                            // Mode 8 (5 bits, 10110)
                        {
                            struct bc6h_mode8
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 8;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gw : 8;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}
                                uint64_t gy5 : 1;  // {GY, 5}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bw : 8;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}
                                uint64_t gz5 : 1;  // {GZ, 5}
                                uint64_t bz4 : 1;  // {BZ, 4}
                                uint64_t rx : 5;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 6;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 5;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 5;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode8) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode8*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4) | (m->gy5 << 5)),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4) | (m->gz5 << 5)),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 8);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 8);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 8);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 5);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 6);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 5);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 5);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 6);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 5);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 5);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 6);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 5);
                            }

                            wprintf(L"\tMode 8 - [8 5 6 5] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x1A:
                            // Mode 9 (5 bits, 11010)
                        {
                            struct bc6h_mode9
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 8;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}
                                uint64_t bz1 : 1;  // {BZ, 1}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gw : 8;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}
                                uint64_t by5 : 1;  // {BY, 5}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bw : 8;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}
                                uint64_t bz5 : 1;  // {BZ, 5}
                                uint64_t bz4 : 1;  // {BZ, 4}
                                uint64_t rx : 5;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 5;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}
                                uint64_t bz0 : 1;  // {BZ, 0}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 6;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 5;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t rz : 5;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode9) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode9*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4)),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4) | (m->by5 << 5)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4)),
                                int(m->bz0 | (m->bz1 << 1) | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4) | (m->bz5 << 5)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 8);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 8);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 8);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 5);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 5);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 6);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 5);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 5);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 6);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 5);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 5);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 6);
                            }

                            wprintf(L"\tMode 9 - [8 5 5 6] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x1E:
                            // Mode 10 (5 bits, 11110)
                        {
                            struct bc6h_mode10
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 6;   // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}
                                uint64_t gz4 : 1;  // {GZ, 4}
                                uint64_t bz : 2;  // {BZ, 0}, {BZ, 1}
                                uint64_t by4 : 1;  // {BY, 4}
                                uint64_t gw : 6;   // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}
                                uint64_t gy5 : 1;  // {GY, 5}
                                uint64_t by5 : 1;  // {BY, 5}
                                uint64_t bz2 : 1;  // {BZ, 2}
                                uint64_t gy4 : 1;  // {GY, 4}
                                uint64_t bw : 6;   // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {GZ, 5}
                                uint64_t bz3 : 1;  // {BZ, 3}
                                uint64_t bz5 : 1;  // {BZ, 5}
                                uint64_t bz4 : 1;  // {BZ, 4}
                                uint64_t rx : 6;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}
                                uint64_t gy : 4;   // {GY, 0}, {GY, 1}, {GY, 2}, {GY, 3}
                                uint64_t gx : 6;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}
                                uint64_t gz : 4;   // {GZ, 0}, {GZ, 1}, {GZ, 2}, {GZ, 3}
                                uint64_t bx : 6;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}
                                uint64_t by : 3;   // {BY, 0}, {BY, 1}, {BY, 2}
                                uint64_t by3 : 1;  // {BY, 3}
                                uint64_t ry : 6;   // {RY, 0}, {RY, 1}, {RY, 2}, {RY, 3}, {RY, 4}, {RY, 5}
                                uint64_t rz : 6;   // {RZ, 0}, {RZ, 1}, {RZ, 2}, {RZ, 3}, {RZ, 4}, {RZ, 5}
                                uint64_t d : 5;    // { D, 0}, { D, 1}, { D, 2}, { D, 3}, { D, 4}
                                uint64_t indices : 46;
                            };
                            static_assert(sizeof(bc6h_mode10) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode10*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));
                            XMINT3 e1_A(int(m->ry),
                                int(m->gy | (m->gy4 << 4) | (m->gy5 << 5)),
                                int(m->by | (m->by3 << 3) | (m->by4 << 4) | (m->by5 << 5)));
                            XMINT3 e1_B(int(m->rz),
                                int(m->gz | (m->gz4 << 4)),
                                int(m->bz | (m->bz2 << 2) | (m->bz3 << 3) | (m->bz4 << 4) | (m->bz5 << 5)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 6);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 6);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 6);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 6);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 6);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 6);

                                e1_A.x = SIGN_EXTEND(e1_A.x, 6);
                                e1_A.y = SIGN_EXTEND(e1_A.y, 6);
                                e1_A.z = SIGN_EXTEND(e1_A.z, 6);

                                e1_B.x = SIGN_EXTEND(e1_B.x, 6);
                                e1_B.y = SIGN_EXTEND(e1_B.y, 6);
                                e1_B.z = SIGN_EXTEND(e1_B.z, 6);
                            }

                            wprintf(L"\tMode 10 - [6 6 6 6] shape %llu\n", m->d);
                            wprintf(L"\t         E0(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E0(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         E1(A): (%04X, %04X, %04X)\n", e1_A.x & 0xFFFF, e1_A.y & 0xFFFF, e1_A.z & 0xFFFF);
                            wprintf(L"\t         E1(B): (%04X, %04X, %04X)\n", e1_B.x & 0xFFFF, e1_B.y & 0xFFFF, e1_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex3bpp(m->indices, 1, m->d);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x03:
                            // Mode 11 (5 bits, 00011)
                        {
                            struct bc6h_mode11
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 10;  // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}, {RX, 6}, {RX, 7}, {RX, 8}, {RX, 9}
                                uint64_t gx : 10;  // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}, {GX, 6}, {GX, 7}, {GX, 8}, {GX, 9}
                                uint64_t bx : 9;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}, {BX, 6}, {BX, 7}, {BX, 8}
                                uint64_t bx9 : 1;  // {BX, 9}
                                uint64_t indices : 63;
                            };
                            static_assert(sizeof(bc6h_mode11) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode11*>(sptr);

                            XMINT3 e0_A(int(m->rw), int(m->gw), int(m->bw));
                            XMINT3 e0_B(int(m->rx), int(m->gx),
                                int(m->bx | (m->bx9 << 9)));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 10);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 10);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 10);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 10);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 10);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 10);
                            }

                            wprintf(L"\tMode 11 - [10 10]\n");
                            wprintf(L"\t         E(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex4bpp(m->indices, 0, 0);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x07:
                            // Mode 12 (5 bits, 00111)
                        {
                            struct bc6h_mode12
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 9;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}, {RX, 6}, {RX, 7}, {RX, 8}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t gx : 9;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}, {GX, 6}, {GX, 7}, {GX, 8}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t bx : 9;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}, {BX, 6}, {BX, 7}, {BX, 8}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t indices : 63;
                            };
                            static_assert(sizeof(bc6h_mode12) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode12*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10)),
                                int(m->gw | (m->gw10 << 10)),
                                int(m->bw | (m->bw10 << 10)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 11);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 11);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 11);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 9);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 9);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 9);
                            }

                            wprintf(L"\tMode 12 - [11 9]\n");
                            wprintf(L"\t         E(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex4bpp(m->indices, 0, 0);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x0B:
                            // Mode 13 (5 bits, 01011)
                        {
                            struct bc6h_mode13
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 8;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}, {RX, 4}, {RX, 5}, {RX, 6}, {RX, 7}
                                uint64_t rw11 : 1; // {RW,11}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t gx : 8;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}, {GX, 4}, {GX, 5}, {GX, 6}, {GX, 7}
                                uint64_t gw11 : 1; // {GW,11}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t bx : 8;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}, {BX, 4}, {BX, 5}, {BX, 6}, {BX, 7}
                                uint64_t bw11 : 1; // {BW,11}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t indices : 63;
                            };
                            static_assert(sizeof(bc6h_mode13) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode13*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10) | (m->rw11 << 11)),
                                int(m->gw | (m->gw10 << 10) | (m->gw11 << 11)),
                                int(m->bw | (m->bw10 << 10) | (m->bw11 << 11)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 12);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 12);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 12);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 8);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 8);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 8);
                            }

                            wprintf(L"\tMode 13 - [12 8]\n");
                            wprintf(L"\t         E(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex4bpp(m->indices, 0, 0);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x0F:
                            // Mode 14 (5 bits, 01111)
                        {
                            struct bc6h_mode14
                            {
                                uint64_t mode : 5; // { M, 0}, { M, 1}, { M, 2}, { M, 3}, { M, 4}
                                uint64_t rw : 10;  // {RW, 0}, {RW, 1}, {RW, 2}, {RW, 3}, {RW, 4}, {RW, 5}, {RW, 6}, {RW, 7}, {RW, 8}, {RW, 9}
                                uint64_t gw : 10;  // {GW, 0}, {GW, 1}, {GW, 2}, {GW, 3}, {GW, 4}, {GW, 5}, {GW, 6}, {GW, 7}, {GW, 8}, {GW, 9}
                                uint64_t bw : 10;  // {BW, 0}, {BW, 1}, {BW, 2}, {BW, 3}, {BW, 4}, {BW, 5}, {BW, 6}, {BW, 7}, {BW, 8}, {BW, 9}
                                uint64_t rx : 4;   // {RX, 0}, {RX, 1}, {RX, 2}, {RX, 3}
                                uint64_t rw15 : 1; // {RW,15}
                                uint64_t rw14 : 1; // {RW,14}
                                uint64_t rw13 : 1; // {RW,13}
                                uint64_t rw12 : 1; // {RW,12}
                                uint64_t rw11 : 1; // {RW,11}
                                uint64_t rw10 : 1; // {RW,10}
                                uint64_t gx : 4;   // {GX, 0}, {GX, 1}, {GX, 2}, {GX, 3}
                                uint64_t gw15 : 1; // {GW,15}
                                uint64_t gw14 : 1; // {GW,14}
                                uint64_t gw13 : 1; // {GW,13}
                                uint64_t gw12 : 1; // {GW,12}
                                uint64_t gw11 : 1; // {GW,11}
                                uint64_t gw10 : 1; // {GW,10}
                                uint64_t bx : 4;   // {BX, 0}, {BX, 1}, {BX, 2}, {BX, 3}
                                uint64_t bw15 : 1; // {BW,15}
                                uint64_t bw14 : 1; // {BW,14}
                                uint64_t bw13 : 1; // {BW,13}
                                uint64_t bw12 : 1; // {BW,12}
                                uint64_t bw11 : 1; // {BW,11}
                                uint64_t bw10 : 1; // {BW,10}
                                uint64_t indices : 63;
                            };
                            static_assert(sizeof(bc6h_mode14) == 16, "Block size must be 16 bytes");

                            bool bSigned = (image.format == DXGI_FORMAT_BC6H_SF16) ? true : false;

                            auto m = reinterpret_cast<const bc6h_mode14*>(sptr);

                            XMINT3 e0_A(int(m->rw | (m->rw10 << 10) | (m->rw11 << 11) | (m->rw12 << 12) | (m->rw13 << 13) | (m->rw14 << 14) | (m->rw15 << 15)),
                                int(m->gw | (m->gw10 << 10) | (m->gw11 << 11) | (m->gw12 << 12) | (m->gw13 << 13) | (m->gw14 << 14) | (m->gw15 << 15)),
                                int(m->bw | (m->bw10 << 10) | (m->bw11 << 11) | (m->bw12 << 12) | (m->bw13 << 13) | (m->bw14 << 14) | (m->bw15 << 15)));
                            XMINT3 e0_B(int(m->rx), int(m->gx), int(m->bx));

                            if (bSigned)
                            {
                                e0_A.x = SIGN_EXTEND(e0_A.x, 16);
                                e0_A.y = SIGN_EXTEND(e0_A.y, 16);
                                e0_A.z = SIGN_EXTEND(e0_A.z, 16);

                                e0_B.x = SIGN_EXTEND(e0_B.x, 4);
                                e0_B.y = SIGN_EXTEND(e0_B.y, 4);
                                e0_B.z = SIGN_EXTEND(e0_B.z, 4);
                            }

                            wprintf(L"\tMode 14 - [16 4]\n");
                            wprintf(L"\t         E(A): (%04X, %04X, %04X)\n", e0_A.x & 0xFFFF, e0_A.y & 0xFFFF, e0_A.z & 0xFFFF);
                            wprintf(L"\t         E(B): (%04X, %04X, %04X)\n", e0_B.x & 0xFFFF, e0_B.y & 0xFFFF, e0_B.z & 0xFFFF);
                            wprintf(L"\t         Index: ");
                            PrintIndex4bpp(m->indices, 0, 0);
                            wprintf(L"\n");
                        }
                        break;

                        case 0x13: // Reserved mode (5 bits, 10011)
                            wprintf(L"\tERROR - Reserved mode 10011\n");
                            break;

                        case 0x17: // Reserved mode (5 bits, 10111)
                            wprintf(L"\tERROR - Reserved mode 10011\n");
                            break;

                        case 0x1B: // Reserved mode (5 bits, 11011)
                            wprintf(L"\tERROR - Reserved mode 11011\n");
                            break;

                        case 0x1F: // Reserved mode (5 bits, 11111)
                            wprintf(L"\tERROR - Reserved mode 11111\n");
                            break;

                        default:
                            break;
                        }
                        break;
                    }
                    break;

                case DXGI_FORMAT_BC7_UNORM:
                case DXGI_FORMAT_BC7_UNORM_SRGB:
                    // http://msdn.microsoft.com/en-us/library/windows/desktop/hh308954.aspx

                    if (*sptr & 0x01)
                    {
                        // Mode 0 (1)
                        struct bc7_mode0
                        {
                            uint64_t mode : 1;
                            uint64_t part : 4;
                            uint64_t r0 : 4;
                            uint64_t r1 : 4;
                            uint64_t r2 : 4;
                            uint64_t r3 : 4;
                            uint64_t r4 : 4;
                            uint64_t r5 : 4;
                            uint64_t g0 : 4;
                            uint64_t g1 : 4;
                            uint64_t g2 : 4;
                            uint64_t g3 : 4;
                            uint64_t g4 : 4;
                            uint64_t g5 : 4;
                            uint64_t b0 : 4;
                            uint64_t b1 : 4;
                            uint64_t b2 : 3;
                            uint64_t b2n : 1;
                            uint64_t b3 : 4;
                            uint64_t b4 : 4;
                            uint64_t b5 : 4;
                            uint64_t P0 : 1;
                            uint64_t P1 : 1;
                            uint64_t P2 : 1;
                            uint64_t P3 : 1;
                            uint64_t P4 : 1;
                            uint64_t P5 : 1;
                            uint64_t index : 45;
                        };
                        static_assert(sizeof(bc7_mode0) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode0*>(sptr);

                        wprintf(L"\tMode 0 - [4 4 4] partition %llu\n", m->part);
                        wprintf(L"\t         E0:(%0.3f, %0.3f, %0.3f)\n", float((m->r0 << 1) | m->P0) / 31.f, float((m->g0 << 1) | m->P0) / 31.f, float((m->b0 << 1) | m->P0) / 31.f);
                        wprintf(L"\t         E1:(%0.3f, %0.3f, %0.3f)\n", float((m->r1 << 1) | m->P1) / 31.f, float((m->g1 << 1) | m->P1) / 31.f, float((m->b1 << 1) | m->P1) / 31.f);
                        wprintf(L"\t         E2:(%0.3f, %0.3f, %0.3f)\n", float((m->r2 << 1) | m->P2) / 31.f, float((m->g2 << 1) | m->P2) / 31.f, float(((m->b2 | (m->b2n << 3)) << 1) | m->P2) / 31.f);
                        wprintf(L"\t         E3:(%0.3f, %0.3f, %0.3f)\n", float((m->r3 << 1) | m->P3) / 31.f, float((m->g3 << 1) | m->P3) / 31.f, float((m->b3 << 1) | m->P3) / 31.f);
                        wprintf(L"\t         E4:(%0.3f, %0.3f, %0.3f)\n", float((m->r4 << 1) | m->P4) / 31.f, float((m->g4 << 1) | m->P4) / 31.f, float((m->b4 << 1) | m->P4) / 31.f);
                        wprintf(L"\t         E5:(%0.3f, %0.3f, %0.3f)\n", float((m->r5 << 1) | m->P5) / 31.f, float((m->g5 << 1) | m->P5) / 31.f, float((m->b5 << 1) | m->P5) / 31.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex2bpp(m->index, 2, m->part);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x02)
                    {
                        // Mode 1 (01)
                        struct bc7_mode1
                        {
                            uint64_t mode : 2;
                            uint64_t part : 6;
                            uint64_t r0 : 6;
                            uint64_t r1 : 6;
                            uint64_t r2 : 6;
                            uint64_t r3 : 6;
                            uint64_t g0 : 6;
                            uint64_t g1 : 6;
                            uint64_t g2 : 6;
                            uint64_t g3 : 6;
                            uint64_t b0 : 6;
                            uint64_t b1 : 2;
                            uint64_t b1n : 4;
                            uint64_t b2 : 6;
                            uint64_t b3 : 6;
                            uint64_t P0 : 1;
                            uint64_t P1 : 1;
                            uint64_t index : 46;
                        };
                        static_assert(sizeof(bc7_mode1) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode1*>(sptr);

                        wprintf(L"\tMode 1 - [6 6 6] partition %llu\n", m->part);
                        wprintf(L"\t         E0:(%0.3f, %0.3f, %0.3f)\n", float((m->r0 << 1) | m->P0) / 127.f, float((m->g0 << 1) | m->P0) / 127.f, float((m->b0 << 1) | m->P0) / 127.f);
                        wprintf(L"\t         E1:(%0.3f, %0.3f, %0.3f)\n", float((m->r1 << 1) | m->P0) / 127.f, float((m->g1 << 1) | m->P0) / 127.f, float(((m->b1 | (m->b1n << 2)) << 1) | m->P0) / 127.f);
                        wprintf(L"\t         E2:(%0.3f, %0.3f, %0.3f)\n", float((m->r2 << 1) | m->P1) / 127.f, float((m->g2 << 1) | m->P1) / 127.f, float((m->b2 << 1) | m->P1) / 127.f);
                        wprintf(L"\t         E3:(%0.3f, %0.3f, %0.3f)\n", float((m->r3 << 1) | m->P1) / 127.f, float((m->g3 << 1) | m->P1) / 127.f, float((m->b3 << 1) | m->P1) / 127.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex3bpp(m->index, 1, m->part);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x04)
                    {
                        // Mode 2 (001)
                        struct bc7_mode2
                        {
                            uint64_t mode : 3;
                            uint64_t part : 6;
                            uint64_t r0 : 5;
                            uint64_t r1 : 5;
                            uint64_t r2 : 5;
                            uint64_t r3 : 5;
                            uint64_t r4 : 5;
                            uint64_t r5 : 5;
                            uint64_t g0 : 5;
                            uint64_t g1 : 5;
                            uint64_t g2 : 5;
                            uint64_t g3 : 5;
                            uint64_t g4 : 5;
                            uint64_t g5 : 5;
                            uint64_t b0 : 5;
                            uint64_t b1 : 5;
                            uint64_t b2 : 5;
                            uint64_t b3 : 5;
                            uint64_t b4 : 5;
                            uint64_t b5 : 5;
                            uint64_t index : 29;
                        };
                        static_assert(sizeof(bc7_mode2) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode2*>(sptr);

                        wprintf(L"\tMode 2 - [5 5 5] partition %llu\n", m->part);
                        wprintf(L"\t         E0:(%0.3f, %0.3f, %0.3f)\n", float(m->r0) / 31.f, float(m->g0) / 31.f, float(m->b0) / 31.f);
                        wprintf(L"\t         E1:(%0.3f, %0.3f, %0.3f)\n", float(m->r1) / 31.f, float(m->g1) / 31.f, float(m->b1) / 31.f);
                        wprintf(L"\t         E2:(%0.3f, %0.3f, %0.3f)\n", float(m->r2) / 31.f, float(m->g2) / 31.f, float(m->b2) / 31.f);
                        wprintf(L"\t         E3:(%0.3f, %0.3f, %0.3f)\n", float(m->r3) / 31.f, float(m->g3) / 31.f, float(m->b3) / 31.f);
                        wprintf(L"\t         E4:(%0.3f, %0.3f, %0.3f)\n", float(m->r4) / 31.f, float(m->g4) / 31.f, float(m->b4) / 31.f);
                        wprintf(L"\t         E5:(%0.3f, %0.3f, %0.3f)\n", float(m->r5) / 31.f, float(m->g5) / 31.f, float(m->b5) / 31.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex2bpp(m->index, 2, m->part);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x08)
                    {
                        // Mode 3 (0001)
                        struct bc7_mode3
                        {
                            uint64_t mode : 4;
                            uint64_t part : 6;
                            uint64_t r0 : 7;
                            uint64_t r1 : 7;
                            uint64_t r2 : 7;
                            uint64_t r3 : 7;
                            uint64_t g0 : 7;
                            uint64_t g1 : 7;
                            uint64_t g2 : 7;
                            uint64_t g3 : 5;
                            uint64_t g3n : 2;
                            uint64_t b0 : 7;
                            uint64_t b1 : 7;
                            uint64_t b2 : 7;
                            uint64_t b3 : 7;
                            uint64_t P0 : 1;
                            uint64_t P1 : 1;
                            uint64_t P2 : 1;
                            uint64_t P3 : 1;
                            uint64_t index : 30;
                        };
                        static_assert(sizeof(bc7_mode3) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode3*>(sptr);

                        wprintf(L"\tMode 3 - [7 7 7] partition %llu\n", m->part);
                        wprintf(L"\t         E0:(%0.3f, %0.3f, %0.3f)\n", float((m->r0 << 1) | m->P0) / 255.f, float((m->g0 << 1) | m->P0) / 255.f, float((m->b0 << 1) | m->P0) / 255.f);
                        wprintf(L"\t         E1:(%0.3f, %0.3f, %0.3f)\n", float((m->r1 << 1) | m->P1) / 255.f, float((m->g1 << 1) | m->P1) / 255.f, float((m->b1 << 1) | m->P1) / 255.f);
                        wprintf(L"\t         E2:(%0.3f, %0.3f, %0.3f)\n", float((m->r2 << 1) | m->P2) / 255.f, float((m->g2 << 1) | m->P2) / 255.f, float((m->b2 << 1) | m->P2) / 255.f);
                        wprintf(L"\t         E3:(%0.3f, %0.3f, %0.3f)\n", float((m->r3 << 1) | m->P3) / 255.f, float(((m->g3 | (m->g3n << 5)) << 1) | m->P3) / 255.f, float((m->b3 << 1) | m->P3) / 255.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex2bpp(m->index, 1, m->part);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x10)
                    {
                        // Mode 4 (00001)
                        struct bc7_mode4
                        {
                            uint64_t mode : 5;
                            uint64_t rot : 2;
                            uint64_t idx : 1;
                            uint64_t r0 : 5;
                            uint64_t r1 : 5;
                            uint64_t g0 : 5;
                            uint64_t g1 : 5;
                            uint64_t b0 : 5;
                            uint64_t b1 : 5;
                            uint64_t a0 : 6;
                            uint64_t a1 : 6;
                            uint64_t color_index : 14;
                            uint64_t color_indexn : 17;
                            uint64_t alpha_index : 47;
                        };
                        static_assert(sizeof(bc7_mode4) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode4*>(sptr);

                        wprintf(L"\tMode 4 - [5 5 5 A6] indx mode %ls, rot-bits %llu%ls\n", m->idx ? L"3-bit" : L"2-bit", m->rot, GetRotBits(m->rot));
                        wprintf(L"\t         C0:(%0.3f, %0.3f, %0.3f)\n", float(m->r0) / 31.f, float(m->g0) / 31.f, float(m->b0) / 31.f);
                        wprintf(L"\t         C1:(%0.3f, %0.3f, %0.3f)\n", float(m->r1) / 31.f, float(m->g1) / 31.f, float(m->b1) / 31.f);
                        wprintf(L"\t         A0:(%0.3f)\n", float(m->a0) / 63.f);
                        wprintf(L"\t         A1:(%0.3f)\n", float(m->a1) / 63.f);
                        wprintf(L"\t    Colors: ");

                        uint64_t color_index = uint64_t(m->color_index) | uint64_t(m->color_indexn << 14);
                        if (m->idx)
                            PrintIndex3bpp(color_index, 0, 0);
                        else
                            PrintIndex2bpp(color_index, 0, 0);
                        wprintf(L"\n");
                        wprintf(L"\t     Alpha: ");
                        PrintIndex3bpp(m->alpha_index, 0, 0);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x20)
                    {
                        // Mode 5 (000001)
                        struct bc7_mode5
                        {
                            uint64_t mode : 6;
                            uint64_t rot : 2;
                            uint64_t r0 : 7;
                            uint64_t r1 : 7;
                            uint64_t g0 : 7;
                            uint64_t g1 : 7;
                            uint64_t b0 : 7;
                            uint64_t b1 : 7;
                            uint64_t a0 : 8;
                            uint64_t a1 : 6;
                            uint64_t a1n : 2;
                            uint64_t color_index : 31;
                            uint64_t alpha_index : 31;
                        };
                        static_assert(sizeof(bc7_mode5) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode5*>(sptr);

                        wprintf(L"\tMode 5 - [7 7 7 A8] rot-bits %llu%ls\n", m->rot, GetRotBits(m->rot));
                        wprintf(L"\t         C0:(%0.3f, %0.3f, %0.3f)\n", float(m->r0) / 127.f, float(m->g0) / 127.f, float(m->b0) / 127.f);
                        wprintf(L"\t         C1:(%0.3f, %0.3f, %0.3f)\n", float(m->r1) / 127.f, float(m->g1) / 127.f, float(m->b1) / 127.f);
                        wprintf(L"\t         A0:(%0.3f)\n", float(m->a0) / 255.f);
                        wprintf(L"\t         A1:(%0.3f)\n", float(m->a1 | (m->a1n << 6)) / 255.f);
                        wprintf(L"\t    Colors: ");
                        PrintIndex2bpp(m->color_index, 0, 0);
                        wprintf(L"\n");
                        wprintf(L"\t     Alpha: ");
                        PrintIndex2bpp(m->alpha_index, 0, 0);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x40)
                    {
                        // Mode 6 (0000001)
                        struct bc7_mode6
                        {
                            uint64_t mode : 7;
                            uint64_t r0 : 7;
                            uint64_t r1 : 7;
                            uint64_t g0 : 7;
                            uint64_t g1 : 7;
                            uint64_t b0 : 7;
                            uint64_t b1 : 7;
                            uint64_t a0 : 7;
                            uint64_t a1 : 7;
                            uint64_t P0 : 1;
                            uint64_t P1 : 1;
                            uint64_t index : 63;

                        };
                        static_assert(sizeof(bc7_mode6) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode6*>(sptr);

                        wprintf(L"\tMode 6 - [7 7 7 A7]\n");
                        wprintf(L"\t         C0:(%0.3f, %0.3f, %0.3f)\n", float((m->r0 << 1) | m->P0) / 255.f, float((m->g0 << 1) | m->P0) / 255.f, float((m->b0 << 1) | m->P0) / 255.f);
                        wprintf(L"\t         C1:(%0.3f, %0.3f, %0.3f)\n", float((m->r1 << 1) | m->P1) / 255.f, float((m->g1 << 1) | m->P1) / 255.f, float((m->b1 << 1) | m->P1) / 255.f);
                        wprintf(L"\t         A0:(%0.3f)\n", float((m->a0 << 1) | m->P0) / 255.f);
                        wprintf(L"\t         A1:(%0.3f)\n", float((m->a1 << 1) | m->P1) / 255.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex4bpp(m->index, 0, 0);
                        wprintf(L"\n");
                    }
                    else if (*sptr & 0x80)
                    {
                        // Mode 7 (00000001)
                        struct bc7_mode7
                        {
                            uint64_t mode : 8;
                            uint64_t part : 6;
                            uint64_t r0 : 5;
                            uint64_t r1 : 5;
                            uint64_t r2 : 5;
                            uint64_t r3 : 5;
                            uint64_t g0 : 5;
                            uint64_t g1 : 5;
                            uint64_t g2 : 5;
                            uint64_t g3 : 5;
                            uint64_t b0 : 5;
                            uint64_t b1 : 5;
                            uint64_t b2 : 5;
                            uint64_t b3 : 5;
                            uint64_t a0 : 5;
                            uint64_t a1 : 5;
                            uint64_t a2 : 5;
                            uint64_t a3 : 5;
                            uint64_t P0 : 1;
                            uint64_t P1 : 1;
                            uint64_t P2 : 1;
                            uint64_t P3 : 1;
                            uint64_t index : 30;

                        };
                        static_assert(sizeof(bc7_mode7) == 16, "Block size must be 16 bytes");

                        auto m = reinterpret_cast<const bc7_mode7*>(sptr);

                        wprintf(L"\tMode 7 - [5 5 5 A5] partition %llu\n", m->part);
                        wprintf(L"\t         C0:(%0.3f, %0.3f, %0.3f)\n", float((m->r0 << 1) | m->P0) / 63.f, float((m->g0 << 1) | m->P0) / 63.f, float((m->b0 << 1) | m->P0) / 63.f);
                        wprintf(L"\t         C1:(%0.3f, %0.3f, %0.3f)\n", float((m->r1 << 1) | m->P1) / 63.f, float((m->g1 << 1) | m->P1) / 63.f, float((m->b1 << 1) | m->P1) / 63.f);
                        wprintf(L"\t         C2:(%0.3f, %0.3f, %0.3f)\n", float((m->r2 << 1) | m->P2) / 63.f, float((m->g2 << 1) | m->P2) / 63.f, float((m->b2 << 1) | m->P2) / 63.f);
                        wprintf(L"\t         C3:(%0.3f, %0.3f, %0.3f)\n", float((m->r3 << 1) | m->P3) / 63.f, float((m->g3 << 1) | m->P3) / 63.f, float((m->b3 << 1) | m->P3) / 63.f);
                        wprintf(L"\t         A0:(%0.3f)\n", float((m->a0 << 1) | m->P0) / 63.f);
                        wprintf(L"\t         A1:(%0.3f)\n", float((m->a1 << 1) | m->P1) / 63.f);
                        wprintf(L"\t         A2:(%0.3f)\n", float((m->a2 << 1) | m->P2) / 63.f);
                        wprintf(L"\t         A3:(%0.3f)\n", float((m->a3 << 1) | m->P3) / 63.f);
                        wprintf(L"\t      Index: ");
                        PrintIndex4bpp(m->index, 1, m->part);
                        wprintf(L"\n");
                    }
                    else
                    {
                        // Reserved mode 8 (00000000)
                        wprintf(L"\tERROR - Reserved mode 8\n");
                    }
                    break;
                }
            }
        }

        return S_OK;
    }
}
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

HRESULT __cdecl LoadFromBMPEx(
    _In_z_ const wchar_t* szFile,
    _In_ WIC_FLAGS flags,
    _Out_opt_ TexMetadata* metadata,
    _Out_ ScratchImage& image) noexcept;

HRESULT __cdecl LoadFromPortablePixMap(
    _In_z_ const wchar_t* szFile,
    _Out_opt_ TexMetadata* metadata,
    _Out_ ScratchImage& image) noexcept;

HRESULT __cdecl SaveToPortablePixMap(
    _In_ const Image& image,
    _In_z_ const wchar_t* szFile) noexcept;

HRESULT __cdecl LoadFromPortablePixMapHDR(
    _In_z_ const wchar_t* szFile,
    _Out_opt_ TexMetadata* metadata,
    _Out_ ScratchImage& image) noexcept;

HRESULT __cdecl SaveToPortablePixMapHDR(
    _In_ const Image& image,
    _In_z_ const wchar_t* szFile) noexcept;

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

#pragma warning( disable : 4616 6211 )

namespace
{
    inline HANDLE safe_handle(HANDLE h) noexcept { return (h == INVALID_HANDLE_VALUE) ? nullptr : h; }

    struct find_closer { void operator()(HANDLE h) noexcept { assert(h != INVALID_HANDLE_VALUE); if (h) FindClose(h); } };

    using ScopedFindHandle = std::unique_ptr<void, find_closer>;

    inline static bool ispow2(size_t x)
    {
        return ((x != 0) && !(x & (x - 1)));
    }

#ifdef _PREFAST_
#pragma prefast(disable : 26018, "Only used with static internal arrays")
#endif

    DWORD LookupByName(const wchar_t *pName, const SValue *pArray)
    {
        while (pArray->pName)
        {
            if (!_wcsicmp(pName, pArray->pName))
                return pArray->dwValue;

            pArray++;
        }

        return 0;
    }

    const wchar_t* LookupByValue(DWORD pValue, const SValue *pArray)
    {
        while (pArray->pName)
        {
            if (pValue == pArray->dwValue)
                return pArray->pName;

            pArray++;
        }

        return L"";
    }

    void SearchForFiles(const wchar_t* path, std::list<SConversion>& files, bool recursive, const wchar_t* folder)
    {
        // Process files
        WIN32_FIND_DATAW findData = {};
        ScopedFindHandle hFile(safe_handle(FindFirstFileExW(path,
            FindExInfoBasic, &findData,
            FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH)));
        if (hFile)
        {
            for (;;)
            {
                if (!(findData.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)))
                {
                    wchar_t drive[_MAX_DRIVE] = {};
                    wchar_t dir[_MAX_DIR] = {};
                    _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

                    SConversion conv = {};
                    _wmakepath_s(conv.szSrc, drive, dir, findData.cFileName, nullptr);
                    if (folder)
                    {
                        wcscpy_s(conv.szFolder, folder);
                    }
                    files.push_back(conv);
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }

        // Process directories
        if (recursive)
        {
            wchar_t searchDir[MAX_PATH] = {};
            {
                wchar_t drive[_MAX_DRIVE] = {};
                wchar_t dir[_MAX_DIR] = {};
                _wsplitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
                _wmakepath_s(searchDir, drive, dir, L"*", nullptr);
            }

            hFile.reset(safe_handle(FindFirstFileExW(searchDir,
                FindExInfoBasic, &findData,
                FindExSearchLimitToDirectories, nullptr,
                FIND_FIRST_EX_LARGE_FETCH)));
            if (!hFile)
                return;

            for (;;)
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (findData.cFileName[0] != L'.')
                    {
                        wchar_t subdir[MAX_PATH] = {};
                        auto subfolder = (folder)
                            ? (std::wstring(folder) + std::wstring(findData.cFileName) + L"\\")
                            : (std::wstring(findData.cFileName) + L"\\");
                        {
                            wchar_t drive[_MAX_DRIVE] = {};
                            wchar_t dir[_MAX_DIR] = {};
                            wchar_t fname[_MAX_FNAME] = {};
                            wchar_t ext[_MAX_FNAME] = {};
                            _wsplitpath_s(path, drive, dir, fname, ext);
                            wcscat_s(dir, findData.cFileName);
                            _wmakepath_s(subdir, drive, dir, fname, ext);
                        }

                        SearchForFiles(subdir, files, recursive, subfolder.c_str());
                    }
                }

                if (!FindNextFileW(hFile.get(), &findData))
                    break;
            }
        }
    }

    void PrintFormat(DXGI_FORMAT Format)
    {
        for (const SValue *pFormat = g_pFormats; pFormat->pName; pFormat++)
        {
            if (static_cast<DXGI_FORMAT>(pFormat->dwValue) == Format)
            {
                wprintf(L"%ls", pFormat->pName);
                return;
            }
        }

        for (const SValue *pFormat = g_pReadOnlyFormats; pFormat->pName; pFormat++)
        {
            if (static_cast<DXGI_FORMAT>(pFormat->dwValue) == Format)
            {
                wprintf(L"%ls", pFormat->pName);
                return;
            }
        }

        wprintf(L"*UNKNOWN*");
    }

    void PrintInfo(const TexMetadata& info)
    {
        wprintf(L" (%zux%zu", info.width, info.height);

        if (TEX_DIMENSION_TEXTURE3D == info.dimension)
            wprintf(L"x%zu", info.depth);

        if (info.mipLevels > 1)
            wprintf(L",%zu", info.mipLevels);

        if (info.arraySize > 1)
            wprintf(L",%zu", info.arraySize);

        wprintf(L" ");
        PrintFormat(info.format);

        switch (info.dimension)
        {
        case TEX_DIMENSION_TEXTURE1D:
            wprintf(L"%ls", (info.arraySize > 1) ? L" 1DArray" : L" 1D");
            break;

        case TEX_DIMENSION_TEXTURE2D:
            if (info.IsCubemap())
            {
                wprintf(L"%ls", (info.arraySize > 6) ? L" CubeArray" : L" Cube");
            }
            else
            {
                wprintf(L"%ls", (info.arraySize > 1) ? L" 2DArray" : L" 2D");
            }
            break;

        case TEX_DIMENSION_TEXTURE3D:
            wprintf(L" 3D");
            break;
        }

        switch (info.GetAlphaMode())
        {
        case TEX_ALPHA_MODE_OPAQUE:
            wprintf(L" \x0e0:Opaque");
            break;
        case TEX_ALPHA_MODE_PREMULTIPLIED:
            wprintf(L" \x0e0:PM");
            break;
        case TEX_ALPHA_MODE_STRAIGHT:
            wprintf(L" \x0e0:NonPM");
            break;
        case TEX_ALPHA_MODE_CUSTOM:
            wprintf(L" \x0e0:Custom");
            break;
        case TEX_ALPHA_MODE_UNKNOWN:
            break;
        }

        wprintf(L")");
    }

    void PrintList(size_t cch, const SValue *pValue)
    {
        while (pValue->pName)
        {
            size_t cchName = wcslen(pValue->pName);

            if (cch + cchName + 2 >= 80)
            {
                wprintf(L"\n      ");
                cch = 6;
            }

            wprintf(L"%ls ", pValue->pName);
            cch += cchName + 2;
            pValue++;
        }

        wprintf(L"\n");
    }

    void PrintLogo()
    {
        wchar_t version[32] = {};

        wchar_t appName[_MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, appName, static_cast<UINT>(std::size(appName))))
        {
            DWORD size = GetFileVersionInfoSizeW(appName, nullptr);
            if (size > 0)
            {
                auto verInfo = std::make_unique<uint8_t[]>(size);
                if (GetFileVersionInfoW(appName, 0, size, verInfo.get()))
                {
                    LPVOID lpstr = nullptr;
                    UINT strLen = 0;
                    if (VerQueryValueW(verInfo.get(), L"\\StringFileInfo\\040904B0\\ProductVersion", &lpstr, &strLen))
                    {
                        wcsncpy_s(version, reinterpret_cast<const wchar_t*>(lpstr), strLen);
                    }
                }
            }
        }

        if (!*version || wcscmp(version, L"1.0.0.0") == 0)
        {
            swprintf_s(version, L"%03d (library)", DIRECTX_TEX_VERSION);
        }

        wprintf(L"Microsoft (R) DirectX Texture Converter [DirectXTex] Version %ls\n", version);
        wprintf(L"Copyright (C) Microsoft Corp. All rights reserved.\n");
#ifdef _DEBUG
        wprintf(L"*** Debug build ***\n");
#endif
        wprintf(L"\n");
    }

    _Success_(return != false)
        bool GetDXGIFactory(_Outptr_ IDXGIFactory1** pFactory)
    {
        if (!pFactory)
            return false;

        *pFactory = nullptr;

        typedef HRESULT(WINAPI* pfn_CreateDXGIFactory1)(REFIID riid, _Out_ void **ppFactory);

        static pfn_CreateDXGIFactory1 s_CreateDXGIFactory1 = nullptr;

        if (!s_CreateDXGIFactory1)
        {
            HMODULE hModDXGI = LoadLibraryW(L"dxgi.dll");
            if (!hModDXGI)
                return false;

            s_CreateDXGIFactory1 = reinterpret_cast<pfn_CreateDXGIFactory1>(reinterpret_cast<void*>(GetProcAddress(hModDXGI, "CreateDXGIFactory1")));
            if (!s_CreateDXGIFactory1)
                return false;
        }

        return SUCCEEDED(s_CreateDXGIFactory1(IID_PPV_ARGS(pFactory)));
    }

    void PrintUsage()
    {
        PrintLogo();

        wprintf(L"Usage: texconvalize <options> <files>\n\n");
        wprintf(L"   -r                  wildcard filename search is recursive\n");
        wprintf(L"     -r:flatten        flatten the directory structure (default)\n");
        wprintf(L"     -r:keep           keep the directory structure\n");
        wprintf(L"   -flist <filename>   use text file with a list of input files (one per line)\n");
        wprintf(L"\n   -w <n>              width\n");
        wprintf(L"   -h <n>              height\n");
        wprintf(L"   -m <n>              miplevels\n");
        wprintf(L"   -f <format>         format\n");
        wprintf(L"\n   -if <filter>        image filtering\n");
        wprintf(L"   -srgb{i|o}          sRGB {input, output}\n");
        wprintf(L"\n   -px <string>        name prefix\n");
        wprintf(L"   -sx <string>        name suffix\n");
        wprintf(L"   -o <directory>      output directory\n");
        wprintf(L"   -l                  force output filename to lower case\n");
        wprintf(L"   -y                  overwrite existing output file (if any)\n");
        wprintf(L"   -ft <filetype>      output file type\n");
        wprintf(L"\n   -hflip              horizonal flip of source image\n");
        wprintf(L"   -vflip              vertical flip of source image\n");
        wprintf(L"\n   -sepalpha           resize/generate mips alpha channel separately\n");
        wprintf(L"                       from color channels\n");
        wprintf(L"   -keepcoverage <ref> Preserve alpha coverage in mips for alpha test ref\n");
        wprintf(L"\n   -nowic              Force non-WIC filtering\n");
        wprintf(L"   -wrap, -mirror      texture addressing mode (wrap, mirror, or clamp)\n");
        wprintf(L"   -pmalpha            convert final texture to use premultiplied alpha\n");
        wprintf(L"   -alpha              convert premultiplied alpha to straight alpha\n");
        wprintf(
            L"   -at <threshold>     Alpha threshold used for BC1, RGBA5551, and WIC\n"
            L"                       (defaults to 0.5)\n");
        wprintf(L"\n   -fl <feature-level> Set maximum feature level target (defaults to 11.0)\n");
        wprintf(L"   -pow2               resize to fit a power-of-2, respecting aspect ratio\n");
        wprintf(
            L"\n   -nmap <options>     converts height-map to normal-map\n"
            L"                       options must be one or more of\n"
            L"                          r, g, b, a, l, m, u, v, i, o\n");
        wprintf(L"   -nmapamp <weight>   normal map amplitude (defaults to 1.0)\n");
        wprintf(L"\n                       (DDS input only)\n");
        wprintf(L"   -t{u|f}             TYPELESS format is treated as UNORM or FLOAT\n");
        wprintf(L"   -dword              Use DWORD instead of BYTE alignment\n");
        wprintf(L"   -badtails           Fix for older DXTn with bad mipchain tails\n");
        wprintf(L"   -fixbc4x4           Fix for odd-sized BC files that Direct3D can't load\n");
        wprintf(L"   -xlum               expand legacy L8, L16, and A8P8 formats\n");
        wprintf(L"\n                       (DDS output only)\n");
        wprintf(L"   -dx10               Force use of 'DX10' extended header\n");
        wprintf(L"   -dx9                Force use of legacy DX9 header\n");
        wprintf(L"\n                       (TGA output only)\n");
        wprintf(L"   -tga20              Write file including TGA 2.0 extension area\n");
        wprintf(L"\n                       (BMP, PNG, JPG, TIF, WDP output only)\n");
        wprintf(L"   -wicq <quality>     When writing images with WIC use quality (0.0 to 1.0)\n");
        wprintf(L"   -wiclossless        When writing images with WIC use lossless mode\n");
        wprintf(L"   -wicmulti           When writing images with WIC encode multiframe images\n");
        wprintf(L"\n   -nologo             suppress copyright message\n");
        wprintf(L"   -timing             Display elapsed processing time\n\n");
#ifdef _OPENMP
        wprintf(L"   -singleproc         Do not use multi-threaded compression\n");
#endif
        wprintf(L"   -gpu <adapter>      Select GPU for DirectCompute-based codecs (0 is default)\n");
        wprintf(L"   -nogpu              Do not use DirectCompute-based codecs\n");
        wprintf(
            L"\n   -bc <options>       Sets options for BC compression\n"
            L"                       options must be one or more of\n"
            L"                          d, u, q, x\n");
        wprintf(
            L"   -aw <weight>        BC7 GPU compressor weighting for alpha error metric\n"
            L"                       (defaults to 1.0)\n");
        wprintf(L"\n   -c <hex-RGB>        colorkey (a.k.a. chromakey) transparency\n");
        wprintf(L"   -rotatecolor <rot>  rotates color primaries and/or applies a curve\n");
        wprintf(L"   -nits <value>       paper-white value in nits to use for HDR10 (def: 200.0)\n");
        wprintf(L"   -tonemap            Apply a tonemap operator based on maximum luminance\n");
        wprintf(L"   -x2bias             Enable *2 - 1 conversion cases for unorm/pos-only-float\n");
        wprintf(L"   -inverty            Invert Y (i.e. green) channel values\n");
        wprintf(L"   -reconstructz       Rebuild Z (blue) channel assuming X/Y are normals\n");
        wprintf(L"   -swizzle <rgba>     Swizzle image channels using HLSL-style mask\n");

        wprintf(L"\n   <format>: ");
        PrintList(13, g_pFormats);
        wprintf(L"      ");
        PrintList(13, g_pFormatAliases);

        wprintf(L"\n   <filter>: ");
        PrintList(13, g_pFilters);

        wprintf(L"\n   <rot>: ");
        PrintList(13, g_pRotateColor);

        wprintf(L"\n   <filetype>: ");
        PrintList(15, g_pSaveFileTypes);

        wprintf(L"\n   <feature-level>: ");
        PrintList(13, g_pFeatureLevels);

        ComPtr<IDXGIFactory1> dxgiFactory;
        if (GetDXGIFactory(dxgiFactory.GetAddressOf()))
        {
            wprintf(L"\n   <adapter>:\n");

            ComPtr<IDXGIAdapter> adapter;
            for (UINT adapterIndex = 0;
                SUCCEEDED(dxgiFactory->EnumAdapters(adapterIndex, adapter.ReleaseAndGetAddressOf()));
                ++adapterIndex)
            {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(adapter->GetDesc(&desc)))
                {
                    wprintf(L"      %u: VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
                }
            }
        }
    }

    _Success_(return != false)
        bool CreateDevice(int adapter, _Outptr_ ID3D11Device** pDevice)
    {
        if (!pDevice)
            return false;

        *pDevice = nullptr;

        static PFN_D3D11_CREATE_DEVICE s_DynamicD3D11CreateDevice = nullptr;

        if (!s_DynamicD3D11CreateDevice)
        {
            HMODULE hModD3D11 = LoadLibraryW(L"d3d11.dll");
            if (!hModD3D11)
                return false;

            s_DynamicD3D11CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(hModD3D11, "D3D11CreateDevice")));
            if (!s_DynamicD3D11CreateDevice)
                return false;
        }

        D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT createDeviceFlags = 0;
#ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        ComPtr<IDXGIAdapter> pAdapter;
        if (adapter >= 0)
        {
            ComPtr<IDXGIFactory1> dxgiFactory;
            if (GetDXGIFactory(dxgiFactory.GetAddressOf()))
            {
                if (FAILED(dxgiFactory->EnumAdapters(static_cast<UINT>(adapter), pAdapter.GetAddressOf())))
                {
                    wprintf(L"\nERROR: Invalid GPU adapter index (%d)!\n", adapter);
                    return false;
                }
            }
        }

        D3D_FEATURE_LEVEL fl;
        HRESULT hr = s_DynamicD3D11CreateDevice(pAdapter.Get(),
            (pAdapter) ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, createDeviceFlags, featureLevels, static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION, pDevice, &fl, nullptr);
        if (SUCCEEDED(hr))
        {
            if (fl < D3D_FEATURE_LEVEL_11_0)
            {
                D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS hwopts;
                hr = (*pDevice)->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &hwopts, sizeof(hwopts));
                if (FAILED(hr))
                    memset(&hwopts, 0, sizeof(hwopts));

                if (!hwopts.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x)
                {
                    if (*pDevice)
                    {
                        (*pDevice)->Release();
                        *pDevice = nullptr;
                    }
                    hr = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            ComPtr<IDXGIDevice> dxgiDevice;
            hr = (*pDevice)->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
            if (SUCCEEDED(hr))
            {
                hr = dxgiDevice->GetAdapter(pAdapter.ReleaseAndGetAddressOf());
                if (SUCCEEDED(hr))
                {
                    DXGI_ADAPTER_DESC desc;
                    hr = pAdapter->GetDesc(&desc);
                    if (SUCCEEDED(hr))
                    {
                        wprintf(L"\n[Using DirectCompute on \"%ls\"]\n", desc.Description);
                    }
                }
            }

            return true;
        }
        else
            return false;
    }

    void FitPowerOf2(size_t origx, size_t origy, size_t& targetx, size_t& targety, size_t maxsize)
    {
        float origAR = float(origx) / float(origy);

        if (origx > origy)
        {
            size_t x;
            for (x = maxsize; x > 1; x >>= 1) { if (x <= targetx) break; }
            targetx = x;

            float bestScore = FLT_MAX;
            for (size_t y = maxsize; y > 0; y >>= 1)
            {
                float score = fabsf((float(x) / float(y)) - origAR);
                if (score < bestScore)
                {
                    bestScore = score;
                    targety = y;
                }
            }
        }
        else
        {
            size_t y;
            for (y = maxsize; y > 1; y >>= 1) { if (y <= targety) break; }
            targety = y;

            float bestScore = FLT_MAX;
            for (size_t x = maxsize; x > 0; x >>= 1)
            {
                float score = fabsf((float(x) / float(y)) - origAR);
                if (score < bestScore)
                {
                    bestScore = score;
                    targetx = x;
                }
            }
        }
    }

    const XMVECTORF32 c_MaxNitsFor2084 = { { { 10000.0f, 10000.0f, 10000.0f, 1.f } } };

    const XMMATRIX c_from709to2020 =
    {
        0.6274040f, 0.0690970f, 0.0163916f, 0.f,
        0.3292820f, 0.9195400f, 0.0880132f, 0.f,
        0.0433136f, 0.0113612f, 0.8955950f, 0.f,
        0.f,        0.f,        0.f,        1.f
    };

    const XMMATRIX c_from2020to709 =
    {
        1.6604910f,  -0.1245505f, -0.0181508f, 0.f,
        -0.5876411f,  1.1328999f, -0.1005789f, 0.f,
        -0.0728499f, -0.0083494f,  1.1187297f, 0.f,
        0.f,          0.f,         0.f,        1.f
    };

    const XMMATRIX c_fromP3to2020 =
    {
        0.753845f, 0.0457456f, -0.00121055f, 0.f,
        0.198593f, 0.941777f,   0.0176041f,  0.f,
        0.047562f, 0.0124772f,  0.983607f,   0.f,
        0.f,       0.f,         0.f,         1.f
    };

    inline float LinearToST2084(float normalizedLinearValue)
    {
        float ST2084 = pow((0.8359375f + 18.8515625f * pow(abs(normalizedLinearValue), 0.1593017578f)) / (1.0f + 18.6875f * pow(abs(normalizedLinearValue), 0.1593017578f)), 78.84375f);
        return ST2084;  // Don't clamp between [0..1], so we can still perform operations on scene values higher than 10,000 nits
    }

    inline float ST2084ToLinear(float ST2084)
    {
        float normalizedLinear = pow(std::max(pow(abs(ST2084), 1.0f / 78.84375f) - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * pow(abs(ST2084), 1.0f / 78.84375f)), 1.0f / 0.1593017578f);
        return normalizedLinear;
    }

    bool ParseSwizzleMask(_In_reads_(4) const wchar_t* mask, _Out_writes_(4) uint32_t* swizzleElements)
    {
        if (!mask || !swizzleElements)
            return false;

        if (!mask[0])
            return false;

        for (size_t j = 0; j < 4; ++j)
        {
            if (!mask[j])
                break;

            switch (mask[j])
            {
            case L'R':
            case L'X':
            case L'r':
            case L'x':
                for (size_t k = j; k < 4; ++k)
                    swizzleElements[k] = 0;
                break;

            case L'G':
            case L'Y':
            case L'g':
            case L'y':
                for (size_t k = j; k < 4; ++k)
                    swizzleElements[k] = 1;
                break;

            case L'B':
            case L'Z':
            case L'b':
            case L'z':
                for (size_t k = j; k < 4; ++k)
                    swizzleElements[k] = 2;
                break;

            case L'A':
            case L'W':
            case L'a':
            case L'w':
                for (size_t k = j; k < 4; ++k)
                    swizzleElements[k] = 3;
                break;

            default:
                return false;
            }
        }

        return true;
    }
}

//--------------------------------------------------------------------------------------
// Entry-point
//--------------------------------------------------------------------------------------
#ifdef _PREFAST_
#pragma prefast(disable : 28198, "Command-line tool, frees all memory on exit")
#endif

int __cdecl wmain(_In_ int argc, _In_z_count_(argc) wchar_t* argv[])
{
    // Parameters and defaults
    size_t width = 0;
    size_t height = 0;
    size_t mipLevels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    TEX_FILTER_FLAGS dwFilter = TEX_FILTER_DEFAULT;
    TEX_FILTER_FLAGS dwSRGB = TEX_FILTER_DEFAULT;
    TEX_FILTER_FLAGS dwConvert = TEX_FILTER_DEFAULT;
    TEX_COMPRESS_FLAGS dwCompress = TEX_COMPRESS_DEFAULT;
    TEX_FILTER_FLAGS dwFilterOpts = TEX_FILTER_DEFAULT;
    DWORD FileType = CODEC_DDS;
    DWORD maxSize = 16384;
    int adapter = -1;
    float alphaThreshold = TEX_THRESHOLD_DEFAULT;
    float alphaWeight = 1.f;
    CNMAP_FLAGS dwNormalMap = CNMAP_DEFAULT;
    float nmapAmplitude = 1.f;
    float wicQuality = -1.f;
    DWORD colorKey = 0;
    DWORD dwRotateColor = 0;
    float paperWhiteNits = 200.f;
    float preserveAlphaCoverageRef = 0.0f;
    bool keepRecursiveDirs = false;
    uint32_t swizzleElements[4] = { 0, 1, 2, 3 };

    wchar_t szPrefix[MAX_PATH] = {};
    wchar_t szSuffix[MAX_PATH] = {};
    wchar_t szOutputDir[MAX_PATH] = {};

    // Initialize COM (needed for WIC)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        wprintf(L"Failed to initialize COM (%08X)\n", static_cast<unsigned int>(hr));
        return 1;
    }

    // Process command line
    DWORD64 dwOptions = 0;
    std::list<SConversion> conversion;

    for (int iArg = 1; iArg < argc; iArg++)
    {
        PWSTR pArg = argv[iArg];

        if (('-' == pArg[0]) || ('/' == pArg[0]))
        {
            pArg++;
            PWSTR pValue;

            for (pValue = pArg; *pValue && (':' != *pValue); pValue++);

            if (*pValue)
                *pValue++ = 0;

            DWORD dwOption = LookupByName(pArg, g_pOptions);

            if (!dwOption || (dwOptions & (DWORD64(1) << dwOption)))
            {
                PrintUsage();
                return 1;
            }

            dwOptions |= (DWORD64(1) << dwOption);

            // Handle options with additional value parameter
            switch (dwOption)
            {
            case OPT_WIDTH:
            case OPT_HEIGHT:
            case OPT_MIPLEVELS:
            case OPT_FORMAT:
            case OPT_FILTER:
            case OPT_PREFIX:
            case OPT_SUFFIX:
            case OPT_OUTPUTDIR:
            case OPT_FILETYPE:
            case OPT_GPU:
            case OPT_FEATURE_LEVEL:
            case OPT_ALPHA_THRESHOLD:
            case OPT_ALPHA_WEIGHT:
            case OPT_NORMAL_MAP:
            case OPT_NORMAL_MAP_AMPLITUDE:
            case OPT_WIC_QUALITY:
            case OPT_BC_COMPRESS:
            case OPT_COLORKEY:
            case OPT_FILELIST:
            case OPT_ROTATE_COLOR:
            case OPT_PAPER_WHITE_NITS:
            case OPT_PRESERVE_ALPHA_COVERAGE:
            case OPT_SWIZZLE:
                // These support either "-arg:value" or "-arg value"
                if (!*pValue)
                {
                    if ((iArg + 1 >= argc))
                    {
                        PrintUsage();
                        return 1;
                    }

                    iArg++;
                    pValue = argv[iArg];
                }
                break;
            }

            switch (dwOption)
            {
            case OPT_WIDTH:
                if (swscanf_s(pValue, L"%zu", &width) != 1)
                {
                    wprintf(L"Invalid value specified with -w (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_HEIGHT:
                if (swscanf_s(pValue, L"%zu", &height) != 1)
                {
                    wprintf(L"Invalid value specified with -h (%ls)\n", pValue);
                    printf("\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_MIPLEVELS:
                if (swscanf_s(pValue, L"%zu", &mipLevels) != 1)
                {
                    wprintf(L"Invalid value specified with -m (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_FORMAT:
                format = static_cast<DXGI_FORMAT>(LookupByName(pValue, g_pFormats));
                if (!format)
                {
                    format = static_cast<DXGI_FORMAT>(LookupByName(pValue, g_pFormatAliases));
                    if (!format)
                    {
                        wprintf(L"Invalid value specified with -f (%ls)\n", pValue);
                        wprintf(L"\n");
                        PrintUsage();
                        return 1;
                    }
                }
                break;

            case OPT_FILTER:
                dwFilter = static_cast<TEX_FILTER_FLAGS>(LookupByName(pValue, g_pFilters));
                if (!dwFilter)
                {
                    wprintf(L"Invalid value specified with -if (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_ROTATE_COLOR:
                dwRotateColor = LookupByName(pValue, g_pRotateColor);
                if (!dwRotateColor)
                {
                    wprintf(L"Invalid value specified with -rotatecolor (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_SRGBI:
                dwSRGB |= TEX_FILTER_SRGB_IN;
                break;

            case OPT_SRGBO:
                dwSRGB |= TEX_FILTER_SRGB_OUT;
                break;

            case OPT_SRGB:
                dwSRGB |= TEX_FILTER_SRGB;
                break;

            case OPT_SEPALPHA:
                dwFilterOpts |= TEX_FILTER_SEPARATE_ALPHA;
                break;

            case OPT_NO_WIC:
                dwFilterOpts |= TEX_FILTER_FORCE_NON_WIC;
                break;

            case OPT_PREFIX:
                wcscpy_s(szPrefix, MAX_PATH, pValue);
                break;

            case OPT_SUFFIX:
                wcscpy_s(szSuffix, MAX_PATH, pValue);
                break;

            case OPT_OUTPUTDIR:
                wcscpy_s(szOutputDir, MAX_PATH, pValue);
                break;

            case OPT_FILETYPE:
                FileType = LookupByName(pValue, g_pSaveFileTypes);
                if (!FileType)
                {
                    wprintf(L"Invalid value specified with -ft (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_PREMUL_ALPHA:
                if (dwOptions & (DWORD64(1) << OPT_DEMUL_ALPHA))
                {
                    wprintf(L"Can't use -pmalpha and -alpha at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_DEMUL_ALPHA:
                if (dwOptions & (DWORD64(1) << OPT_PREMUL_ALPHA))
                {
                    wprintf(L"Can't use -pmalpha and -alpha at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_TA_WRAP:
                if (dwFilterOpts & TEX_FILTER_MIRROR)
                {
                    wprintf(L"Can't use -wrap and -mirror at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                dwFilterOpts |= TEX_FILTER_WRAP;
                break;

            case OPT_TA_MIRROR:
                if (dwFilterOpts & TEX_FILTER_WRAP)
                {
                    wprintf(L"Can't use -wrap and -mirror at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                dwFilterOpts |= TEX_FILTER_MIRROR;
                break;

            case OPT_NORMAL_MAP:
            {
                dwNormalMap = CNMAP_DEFAULT;

                if (wcschr(pValue, L'l'))
                {
                    dwNormalMap |= CNMAP_CHANNEL_LUMINANCE;
                }
                else if (wcschr(pValue, L'r'))
                {
                    dwNormalMap |= CNMAP_CHANNEL_RED;
                }
                else if (wcschr(pValue, L'g'))
                {
                    dwNormalMap |= CNMAP_CHANNEL_GREEN;
                }
                else if (wcschr(pValue, L'b'))
                {
                    dwNormalMap |= CNMAP_CHANNEL_BLUE;
                }
                else if (wcschr(pValue, L'a'))
                {
                    dwNormalMap |= CNMAP_CHANNEL_ALPHA;
                }
                else
                {
                    wprintf(L"Invalid value specified for -nmap (%ls), missing l, r, g, b, or a\n\n", pValue);
                    return 1;
                }

                if (wcschr(pValue, L'm'))
                {
                    dwNormalMap |= CNMAP_MIRROR;
                }
                else
                {
                    if (wcschr(pValue, L'u'))
                    {
                        dwNormalMap |= CNMAP_MIRROR_U;
                    }
                    if (wcschr(pValue, L'v'))
                    {
                        dwNormalMap |= CNMAP_MIRROR_V;
                    }
                }

                if (wcschr(pValue, L'i'))
                {
                    dwNormalMap |= CNMAP_INVERT_SIGN;
                }

                if (wcschr(pValue, L'o'))
                {
                    dwNormalMap |= CNMAP_COMPUTE_OCCLUSION;
                }
            }
            break;

            case OPT_NORMAL_MAP_AMPLITUDE:
                if (!dwNormalMap)
                {
                    wprintf(L"-nmapamp requires -nmap\n\n");
                    PrintUsage();
                    return 1;
                }
                else if (swscanf_s(pValue, L"%f", &nmapAmplitude) != 1)
                {
                    wprintf(L"Invalid value specified with -nmapamp (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                else if (nmapAmplitude < 0.f)
                {
                    wprintf(L"Normal map amplitude must be positive (%ls)\n\n", pValue);
                    return 1;
                }
                break;

            case OPT_GPU:
                if (swscanf_s(pValue, L"%d", &adapter) != 1)
                {
                    wprintf(L"Invalid value specified with -gpu (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                else if (adapter < 0)
                {
                    wprintf(L"Invalid adapter index (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_FEATURE_LEVEL:
                maxSize = LookupByName(pValue, g_pFeatureLevels);
                if (!maxSize)
                {
                    wprintf(L"Invalid value specified with -fl (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_ALPHA_THRESHOLD:
                if (swscanf_s(pValue, L"%f", &alphaThreshold) != 1)
                {
                    wprintf(L"Invalid value specified with -at (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                else if (alphaThreshold < 0.f)
                {
                    wprintf(L"-at (%ls) parameter must be positive\n", pValue);
                    wprintf(L"\n");
                    return 1;
                }
                break;

            case OPT_ALPHA_WEIGHT:
                if (swscanf_s(pValue, L"%f", &alphaWeight) != 1)
                {
                    wprintf(L"Invalid value specified with -aw (%ls)\n", pValue);
                    wprintf(L"\n");
                    PrintUsage();
                    return 1;
                }
                else if (alphaWeight < 0.f)
                {
                    wprintf(L"-aw (%ls) parameter must be positive\n", pValue);
                    wprintf(L"\n");
                    return 1;
                }
                break;

            case OPT_BC_COMPRESS:
            {
                dwCompress = TEX_COMPRESS_DEFAULT;

                bool found = false;
                if (wcschr(pValue, L'u'))
                {
                    dwCompress |= TEX_COMPRESS_UNIFORM;
                    found = true;
                }

                if (wcschr(pValue, L'd'))
                {
                    dwCompress |= TEX_COMPRESS_DITHER;
                    found = true;
                }

                if (wcschr(pValue, L'q'))
                {
                    dwCompress |= TEX_COMPRESS_BC7_QUICK;
                    found = true;
                }

                if (wcschr(pValue, L'x'))
                {
                    dwCompress |= TEX_COMPRESS_BC7_USE_3SUBSETS;
                    found = true;
                }

                if ((dwCompress & (TEX_COMPRESS_BC7_QUICK | TEX_COMPRESS_BC7_USE_3SUBSETS)) == (TEX_COMPRESS_BC7_QUICK | TEX_COMPRESS_BC7_USE_3SUBSETS))
                {
                    wprintf(L"Can't use -bc x (max) and -bc q (quick) at same time\n\n");
                    PrintUsage();
                    return 1;
                }

                if (!found)
                {
                    wprintf(L"Invalid value specified for -bc (%ls), missing d, u, q, or x\n\n", pValue);
                    return 1;
                }
            }
            break;

            case OPT_WIC_QUALITY:
                if (swscanf_s(pValue, L"%f", &wicQuality) != 1
                    || (wicQuality < 0.f)
                    || (wicQuality > 1.f))
                {
                    wprintf(L"Invalid value specified with -wicq (%ls)\n", pValue);
                    printf("\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_COLORKEY:
                if (swscanf_s(pValue, L"%lx", &colorKey) != 1)
                {
                    printf("Invalid value specified with -c (%ls)\n", pValue);
                    printf("\n");
                    PrintUsage();
                    return 1;
                }
                colorKey &= 0xFFFFFF;
                break;

            case OPT_X2_BIAS:
                dwConvert |= TEX_FILTER_FLOAT_X2BIAS;
                break;

            case OPT_USE_DX10:
                if (dwOptions & (DWORD64(1) << OPT_USE_DX9))
                {
                    wprintf(L"Can't use -dx9 and -dx10 at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_USE_DX9:
                if (dwOptions & (DWORD64(1) << OPT_USE_DX10))
                {
                    wprintf(L"Can't use -dx9 and -dx10 at same time\n\n");
                    PrintUsage();
                    return 1;
                }
                break;

            case OPT_RECURSIVE:
                if (*pValue)
                {
                    // This option takes 'flatten' or 'keep' with ':' syntax
                    if (!_wcsicmp(pValue, L"keep"))
                    {
                        keepRecursiveDirs = true;
                    }
                    else if (_wcsicmp(pValue, L"flatten") != 0)
                    {
                        wprintf(L"For recursive use -r, -r:flatten, or -r:keep\n\n");
                        PrintUsage();
                        return 1;
                    }
                }
                break;

            case OPT_FILELIST:
            {
                std::wifstream inFile(pValue);
                if (!inFile)
                {
                    wprintf(L"Error opening -flist file %ls\n", pValue);
                    return 1;
                }
                wchar_t fname[1024] = {};
                for (;;)
                {
                    inFile >> fname;
                    if (!inFile)
                        break;

                    if (*fname == L'#')
                    {
                        // Comment
                    }
                    else if (*fname == L'-')
                    {
                        wprintf(L"Command-line arguments not supported in -flist file\n");
                        return 1;
                    }
                    else if (wcspbrk(fname, L"?*") != nullptr)
                    {
                        wprintf(L"Wildcards not supported in -flist file\n");
                        return 1;
                    }
                    else
                    {
                        SConversion conv = {};
                        wcscpy_s(conv.szSrc, MAX_PATH, fname);
                        conversion.push_back(conv);
                    }

                    inFile.ignore(1000, '\n');
                }
                inFile.close();
            }
            break;

            case OPT_PAPER_WHITE_NITS:
                if (swscanf_s(pValue, L"%f", &paperWhiteNits) != 1)
                {
                    wprintf(L"Invalid value specified with -nits (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                else if (paperWhiteNits > 10000.f || paperWhiteNits <= 0.f)
                {
                    wprintf(L"-nits (%ls) parameter must be between 0 and 10000\n\n", pValue);
                    return 1;
                }
                break;

            case OPT_PRESERVE_ALPHA_COVERAGE:
                if (swscanf_s(pValue, L"%f", &preserveAlphaCoverageRef) != 1)
                {
                    wprintf(L"Invalid value specified with -keepcoverage (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                else if (preserveAlphaCoverageRef < 0.0f || preserveAlphaCoverageRef > 1.0f)
                {
                    wprintf(L"-keepcoverage (%ls) parameter must be between 0.0 and 1.0\n\n", pValue);
                    return 1;
                }
                break;

            case OPT_SWIZZLE:
                if (!*pValue || wcslen(pValue) > 4)
                {
                    wprintf(L"Invalid value specified with -swizzle (%ls)\n\n", pValue);
                    PrintUsage();
                    return 1;
                }
                else if (!ParseSwizzleMask(pValue, swizzleElements))
                {
                    wprintf(L"-swizzle requires a 1 to 4 character mask composed of these letters: r, g, b, a, x, y, w, z\n");
                    return 1;
                }
                break;
            }
        }
        else if (wcspbrk(pArg, L"?*") != nullptr)
        {
            size_t count = conversion.size();
            SearchForFiles(pArg, conversion, (dwOptions & (DWORD64(1) << OPT_RECURSIVE)) != 0, nullptr);
            if (conversion.size() <= count)
            {
                wprintf(L"No matching files found for %ls\n", pArg);
                return 1;
            }
        }
        else
        {
            SConversion conv = {};
            wcscpy_s(conv.szSrc, MAX_PATH, pArg);

            conversion.push_back(conv);
        }
    }

    if (conversion.empty())
    {
        PrintUsage();
        return 0;
    }

    if (~dwOptions & (DWORD64(1) << OPT_NOLOGO))
        PrintLogo();

    // Work out out filename prefix and suffix
    if (szOutputDir[0] && (L'\\' != szOutputDir[wcslen(szOutputDir) - 1]))
        wcscat_s(szOutputDir, MAX_PATH, L"\\");

    auto fileTypeName = LookupByValue(FileType, g_pSaveFileTypes);

    if (fileTypeName)
    {
        wcscat_s(szSuffix, MAX_PATH, L".");
        wcscat_s(szSuffix, MAX_PATH, fileTypeName);
    }
    else
    {
        wcscat_s(szSuffix, MAX_PATH, L".unknown");
    }

    if (FileType != CODEC_DDS)
    {
        mipLevels = 1;
    }

    LARGE_INTEGER qpcFreq;
    if (!QueryPerformanceFrequency(&qpcFreq))
    {
        qpcFreq.QuadPart = 0;
    }

    LARGE_INTEGER qpcStart;
    if (!QueryPerformanceCounter(&qpcStart))
    {
        qpcStart.QuadPart = 0;
    }

    // Convert images
    bool sizewarn = false;
    bool nonpow2warn = false;
    bool non4bc = false;
    bool preserveAlphaCoverage = false;
    ComPtr<ID3D11Device> pDevice;

    for (auto pConv = conversion.begin(); pConv != conversion.end(); ++pConv)
    {
        if (pConv != conversion.begin())
            wprintf(L"\n");

        // --- Load source image -------------------------------------------------------
        wprintf(L"reading %ls", pConv->szSrc);
        fflush(stdout);

        wchar_t ext[_MAX_EXT] = {};
        wchar_t fname[_MAX_FNAME] = {};
        _wsplitpath_s(pConv->szSrc, nullptr, 0, nullptr, 0, fname, _MAX_FNAME, ext, _MAX_EXT);

        TexMetadata info;
        std::unique_ptr<ScratchImage> image(new (std::nothrow) ScratchImage);

        if (!image)
        {
            wprintf(L"\nERROR: Memory allocation failed\n");
            return 1;
        }

        if (_wcsicmp(ext, L".dds") == 0)
        {
            DDS_FLAGS ddsFlags = DDS_FLAGS_ALLOW_LARGE_FILES;
            if (dwOptions & (DWORD64(1) << OPT_DDS_DWORD_ALIGN))
                ddsFlags |= DDS_FLAGS_LEGACY_DWORD;
            if (dwOptions & (DWORD64(1) << OPT_EXPAND_LUMINANCE))
                ddsFlags |= DDS_FLAGS_EXPAND_LUMINANCE;
            if (dwOptions & (DWORD64(1) << OPT_DDS_BAD_DXTN_TAILS))
                ddsFlags |= DDS_FLAGS_BAD_DXTN_TAILS;

            hr = LoadFromDDSFile(pConv->szSrc, ddsFlags, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }

            if (IsTypeless(info.format))
            {
                if (dwOptions & (DWORD64(1) << OPT_TYPELESS_UNORM))
                {
                    info.format = MakeTypelessUNORM(info.format);
                }
                else if (dwOptions & (DWORD64(1) << OPT_TYPELESS_FLOAT))
                {
                    info.format = MakeTypelessFLOAT(info.format);
                }

                if (IsTypeless(info.format))
                {
                    wprintf(L" FAILED due to Typeless format %d\n", info.format);
                    continue;
                }

                image->OverrideFormat(info.format);
            }
        }
        else if (_wcsicmp(ext, L".bmp") == 0)
        {
            hr = LoadFromBMPEx(pConv->szSrc, WIC_FLAGS_NONE | dwFilter, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
        else if (_wcsicmp(ext, L".tga") == 0)
        {
            hr = LoadFromTGAFile(pConv->szSrc, TGA_FLAGS_NONE, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
        else if (_wcsicmp(ext, L".hdr") == 0)
        {
            hr = LoadFromHDRFile(pConv->szSrc, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
        else if (_wcsicmp(ext, L".ppm") == 0)
        {
            hr = LoadFromPortablePixMap(pConv->szSrc, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
        else if (_wcsicmp(ext, L".pfm") == 0)
        {
            hr = LoadFromPortablePixMapHDR(pConv->szSrc, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
#ifdef USE_OPENEXR
        else if (_wcsicmp(ext, L".exr") == 0)
        {
            hr = LoadFromEXRFile(pConv->szSrc, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }
#endif
        else
        {
            // WIC shares the same filter values for mode and dither
            static_assert(static_cast<int>(WIC_FLAGS_DITHER) == static_cast<int>(TEX_FILTER_DITHER), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_DITHER_DIFFUSION) == static_cast<int>(TEX_FILTER_DITHER_DIFFUSION), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_POINT) == static_cast<int>(TEX_FILTER_POINT), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_LINEAR) == static_cast<int>(TEX_FILTER_LINEAR), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_CUBIC) == static_cast<int>(TEX_FILTER_CUBIC), "WIC_FLAGS_* & TEX_FILTER_* should match");
            static_assert(static_cast<int>(WIC_FLAGS_FILTER_FANT) == static_cast<int>(TEX_FILTER_FANT), "WIC_FLAGS_* & TEX_FILTER_* should match");

            WIC_FLAGS wicFlags = WIC_FLAGS_NONE | dwFilter;
            if (FileType == CODEC_DDS)
                wicFlags |= WIC_FLAGS_ALL_FRAMES;

            hr = LoadFromWICFile(pConv->szSrc, wicFlags, &info, *image);
            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
        }

        PrintInfo(info);

        size_t tMips = (!mipLevels && info.mipLevels > 1) ? info.mipLevels : mipLevels;

        // Convert texture
        wprintf(L" as");
        fflush(stdout);

        // --- Planar ------------------------------------------------------------------
        if (IsPlanar(info.format))
        {
            auto img = image->GetImage(0, 0, 0);
            assert(img);
            size_t nimg = image->GetImageCount();

            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = ConvertToSinglePlane(img, nimg, info, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [converttosingleplane] (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }

            auto& tinfo = timage->GetMetadata();

            info.format = tinfo.format;

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
        }

        DXGI_FORMAT tformat = (format == DXGI_FORMAT_UNKNOWN) ? info.format : format;

        // --- Decompress --------------------------------------------------------------
        std::unique_ptr<ScratchImage> cimage;
        if (IsCompressed(info.format))
        {
            // Direct3D can only create BC resources with multiple-of-4 top levels
            if ((info.width % 4) != 0 || (info.height % 4) != 0)
            {
                if (dwOptions & (DWORD64(1) << OPT_BCNONMULT4FIX))
                {
                    std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
                    if (!timage)
                    {
                        wprintf(L"\nERROR: Memory allocation failed\n");
                        return 1;
                    }

                    // If we started with < 4x4 then no need to generate mips
                    if (info.width < 4 && info.height < 4)
                    {
                        tMips = 1;
                    }

                    // Fix by changing size but also have to trim any mip-levels which can be invalid
                    TexMetadata mdata = image->GetMetadata();
                    mdata.width = (info.width + 3u) & ~0x3u;
                    mdata.height = (info.height + 3u) & ~0x3u;
                    mdata.mipLevels = 1;
                    hr = timage->Initialize(mdata);
                    if (FAILED(hr))
                    {
                        wprintf(L" FAILED [BC non-multiple-of-4 fixup] (%x)\n", static_cast<unsigned int>(hr));
                        return 1;
                    }

                    if (mdata.dimension == TEX_DIMENSION_TEXTURE3D)
                    {
                        for (size_t d = 0; d < mdata.depth; ++d)
                        {
                            auto simg = image->GetImage(0, 0, d);
                            auto dimg = timage->GetImage(0, 0, d);

                            memcpy_s(dimg->pixels, dimg->slicePitch, simg->pixels, simg->slicePitch);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < mdata.arraySize; ++i)
                        {
                            auto simg = image->GetImage(0, i, 0);
                            auto dimg = timage->GetImage(0, i, 0);

                            memcpy_s(dimg->pixels, dimg->slicePitch, simg->pixels, simg->slicePitch);
                        }
                    }

                    info.width = mdata.width;
                    info.height = mdata.height;
                    info.mipLevels = mdata.mipLevels;
                    image.swap(timage);
                }
                else if (IsCompressed(tformat))
                {
                    non4bc = true;
                }
            }

            auto img = image->GetImage(0, 0, 0);
            assert(img);
            size_t nimg = image->GetImageCount();

            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = Decompress(img, nimg, info, DXGI_FORMAT_UNKNOWN /* picks good default */, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [decompress] (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }

            auto& tinfo = timage->GetMetadata();

            info.format = tinfo.format;

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.dimension == tinfo.dimension);

            if (FileType == CODEC_DDS)
            {
                // Keep the original compressed image in case we can reuse it
                cimage.reset(image.release());
                image.reset(timage.release());
            }
            else
            {
                image.swap(timage);
            }
        }

        // --- Undo Premultiplied Alpha (if requested) ---------------------------------
        if ((dwOptions & (DWORD64(1) << OPT_DEMUL_ALPHA))
            && HasAlpha(info.format)
            && info.format != DXGI_FORMAT_A8_UNORM)
        {
            if (info.GetAlphaMode() == TEX_ALPHA_MODE_STRAIGHT)
            {
                printf("\nWARNING: Image is already using straight alpha\n");
            }
            else if (!info.IsPMAlpha())
            {
                printf("\nWARNING: Image is not using premultipled alpha\n");
            }
            else
            {
                auto img = image->GetImage(0, 0, 0);
                assert(img);
                size_t nimg = image->GetImageCount();

                std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
                if (!timage)
                {
                    wprintf(L"\nERROR: Memory allocation failed\n");
                    return 1;
                }

                hr = PremultiplyAlpha(img, nimg, info, TEX_PMALPHA_REVERSE | dwSRGB, *timage);
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [demultiply alpha] (%x)\n", static_cast<unsigned int>(hr));
                    continue;
                }

                auto& tinfo = timage->GetMetadata();
                info.miscFlags2 = tinfo.miscFlags2;

                assert(info.width == tinfo.width);
                assert(info.height == tinfo.height);
                assert(info.depth == tinfo.depth);
                assert(info.arraySize == tinfo.arraySize);
                assert(info.mipLevels == tinfo.mipLevels);
                assert(info.miscFlags == tinfo.miscFlags);
                assert(info.dimension == tinfo.dimension);

                image.swap(timage);
                cimage.reset();
            }
        }

        // --- Flip/Rotate -------------------------------------------------------------
        if (dwOptions & ((DWORD64(1) << OPT_HFLIP) | (DWORD64(1) << OPT_VFLIP)))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            TEX_FR_FLAGS dwFlags = TEX_FR_ROTATE0;

            if (dwOptions & (DWORD64(1) << OPT_HFLIP))
                dwFlags |= TEX_FR_FLIP_HORIZONTAL;

            if (dwOptions & (DWORD64(1) << OPT_VFLIP))
                dwFlags |= TEX_FR_FLIP_VERTICAL;

            assert(dwFlags != 0);

            hr = FlipRotate(image->GetImages(), image->GetImageCount(), image->GetMetadata(), dwFlags, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [fliprotate] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            auto& tinfo = timage->GetMetadata();

            info.width = tinfo.width;
            info.height = tinfo.height;

            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Resize ------------------------------------------------------------------
        size_t twidth = (!width) ? info.width : width;
        if (twidth > maxSize)
        {
            if (!width)
                twidth = maxSize;
            else
                sizewarn = true;
        }

        size_t theight = (!height) ? info.height : height;
        if (theight > maxSize)
        {
            if (!height)
                theight = maxSize;
            else
                sizewarn = true;
        }

        if (dwOptions & (DWORD64(1) << OPT_FIT_POWEROF2))
        {
            FitPowerOf2(info.width, info.height, twidth, theight, maxSize);
        }

        if (info.width != twidth || info.height != theight)
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = Resize(image->GetImages(), image->GetImageCount(), image->GetMetadata(), twidth, theight, dwFilter | dwFilterOpts, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [resize] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            auto& tinfo = timage->GetMetadata();

            assert(tinfo.width == twidth && tinfo.height == theight && tinfo.mipLevels == 1);
            info.width = tinfo.width;
            info.height = tinfo.height;
            info.mipLevels = 1;

            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Swizzle (if requested) --------------------------------------------------
        if (swizzleElements[0] != 0 || swizzleElements[1] != 1 || swizzleElements[2] != 2 || swizzleElements[3] != 3)
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < w; ++j)
                    {
                        outPixels[j] = XMVectorSwizzle(inPixels[j],
                            swizzleElements[0], swizzleElements[1], swizzleElements[2], swizzleElements[3]);
                    }
                }, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [swizzle] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Color rotation (if requested) -------------------------------------------
        if (dwRotateColor)
        {
            if (dwRotateColor == ROTATE_HDR10_TO_709)
            {
                std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
                if (!timage)
                {
                    wprintf(L"\nERROR: Memory allocation failed\n");
                    return 1;
                }

                hr = Convert(image->GetImages(), image->GetImageCount(), image->GetMetadata(), DXGI_FORMAT_R16G16B16A16_FLOAT,
                    dwFilter | dwFilterOpts | dwSRGB | dwConvert, alphaThreshold, *timage);
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [convert] (%x)\n", static_cast<unsigned int>(hr));
                    return 1;
                }

#ifndef NDEBUG
                auto& tinfo = timage->GetMetadata();
#endif

                assert(tinfo.format == DXGI_FORMAT_R16G16B16A16_FLOAT);
                info.format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                assert(info.width == tinfo.width);
                assert(info.height == tinfo.height);
                assert(info.depth == tinfo.depth);
                assert(info.arraySize == tinfo.arraySize);
                assert(info.mipLevels == tinfo.mipLevels);
                assert(info.miscFlags == tinfo.miscFlags);
                assert(info.dimension == tinfo.dimension);

                image.swap(timage);
                cimage.reset();
            }

            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            switch (dwRotateColor)
            {
            case ROTATE_709_TO_HDR10:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        XMVECTOR paperWhite = XMVectorReplicate(paperWhiteNits);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            XMVECTOR nvalue = XMVector3Transform(value, c_from709to2020);

                            // Convert to ST.2084
                            nvalue = XMVectorDivide(XMVectorMultiply(nvalue, paperWhite), c_MaxNitsFor2084);

                            XMFLOAT4A tmp;
                            XMStoreFloat4A(&tmp, nvalue);

                            tmp.x = LinearToST2084(tmp.x);
                            tmp.y = LinearToST2084(tmp.y);
                            tmp.z = LinearToST2084(tmp.z);

                            nvalue = XMLoadFloat4A(&tmp);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            case ROTATE_709_TO_2020:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            XMVECTOR nvalue = XMVector3Transform(value, c_from709to2020);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            case ROTATE_HDR10_TO_709:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        XMVECTOR paperWhite = XMVectorReplicate(paperWhiteNits);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            // Convert from ST.2084
                            XMFLOAT4A tmp;
                            XMStoreFloat4A(&tmp, value);

                            tmp.x = ST2084ToLinear(tmp.x);
                            tmp.y = ST2084ToLinear(tmp.y);
                            tmp.z = ST2084ToLinear(tmp.z);

                            XMVECTOR nvalue = XMLoadFloat4A(&tmp);

                            nvalue = XMVectorDivide(XMVectorMultiply(nvalue, c_MaxNitsFor2084), paperWhite);

                            nvalue = XMVector3Transform(nvalue, c_from2020to709);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            case ROTATE_2020_TO_709:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            XMVECTOR nvalue = XMVector3Transform(value, c_from2020to709);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            case ROTATE_P3_TO_HDR10:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        XMVECTOR paperWhite = XMVectorReplicate(paperWhiteNits);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            XMVECTOR nvalue = XMVector3Transform(value, c_fromP3to2020);

                            // Convert to ST.2084
                            nvalue = XMVectorDivide(XMVectorMultiply(nvalue, paperWhite), c_MaxNitsFor2084);

                            XMFLOAT4A tmp;
                            XMStoreFloat4A(&tmp, nvalue);

                            tmp.x = LinearToST2084(tmp.x);
                            tmp.y = LinearToST2084(tmp.y);
                            tmp.z = LinearToST2084(tmp.z);

                            nvalue = XMLoadFloat4A(&tmp);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            case ROTATE_P3_TO_2020:
                hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                    [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                    {
                        UNREFERENCED_PARAMETER(y);

                        for (size_t j = 0; j < w; ++j)
                        {
                            XMVECTOR value = inPixels[j];

                            XMVECTOR nvalue = XMVector3Transform(value, c_fromP3to2020);

                            value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                            outPixels[j] = value;
                        }
                    }, *timage);
                break;

            default:
                hr = E_NOTIMPL;
                break;
            }
            if (FAILED(hr))
            {
                wprintf(L" FAILED [rotate color apply] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Tonemap (if requested) --------------------------------------------------
        if (dwOptions & DWORD64(1) << OPT_TONEMAP)
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            // Compute max luminosity across all images
            XMVECTOR maxLum = XMVectorZero();
            hr = EvaluateImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](const XMVECTOR* pixels, size_t w, size_t y)
                {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < w; ++j)
                    {
                        static const XMVECTORF32 s_luminance = { { { 0.3f, 0.59f, 0.11f, 0.f } } };

                        XMVECTOR v = *pixels++;

                        v = XMVector3Dot(v, s_luminance);

                        maxLum = XMVectorMax(v, maxLum);
                    }
                });
            if (FAILED(hr))
            {
                wprintf(L" FAILED [tonemap maxlum] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            // Reinhard et al, "Photographic Tone Reproduction for Digital Images"
            // http://www.cs.utah.edu/~reinhard/cdrom/
            maxLum = XMVectorMultiply(maxLum, maxLum);

            hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                {
                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < w; ++j)
                    {
                        XMVECTOR value = inPixels[j];

                        XMVECTOR scale = XMVectorDivide(
                            XMVectorAdd(g_XMOne, XMVectorDivide(value, maxLum)),
                            XMVectorAdd(g_XMOne, value));
                        XMVECTOR nvalue = XMVectorMultiply(value, scale);

                        value = XMVectorSelect(value, nvalue, g_XMSelect1110);

                        outPixels[j] = value;
                    }
                }, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [tonemap apply] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Convert -----------------------------------------------------------------
        if (dwOptions & (DWORD64(1) << OPT_NORMAL_MAP))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            DXGI_FORMAT nmfmt = tformat;
            if (IsCompressed(tformat))
            {
                switch (tformat)
                {
                case DXGI_FORMAT_BC4_SNORM:
                case DXGI_FORMAT_BC5_SNORM:
                    nmfmt = DXGI_FORMAT_R8G8B8A8_SNORM;
                    break;

                case DXGI_FORMAT_BC6H_SF16:
                case DXGI_FORMAT_BC6H_UF16:
                    nmfmt = DXGI_FORMAT_R32G32B32_FLOAT;
                    break;

                default:
                    nmfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
                    break;
                }
            }

            hr = ComputeNormalMap(image->GetImages(), image->GetImageCount(), image->GetMetadata(), dwNormalMap, nmapAmplitude, nmfmt, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [normalmap] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            auto& tinfo = timage->GetMetadata();

            assert(tinfo.format == nmfmt);
            info.format = tinfo.format;

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }
        else if (info.format != tformat && !IsCompressed(tformat))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = Convert(image->GetImages(), image->GetImageCount(), image->GetMetadata(), tformat,
                dwFilter | dwFilterOpts | dwSRGB | dwConvert, alphaThreshold, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [convert] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            auto& tinfo = timage->GetMetadata();

            assert(tinfo.format == tformat);
            info.format = tinfo.format;

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- ColorKey/ChromaKey ------------------------------------------------------
        if ((dwOptions & (DWORD64(1) << OPT_COLORKEY))
            && HasAlpha(info.format))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            XMVECTOR colorKeyValue = XMLoadColor(reinterpret_cast<const XMCOLOR*>(&colorKey));

            hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                {
                    static const XMVECTORF32 s_tolerance = { { { 0.2f, 0.2f, 0.2f, 0.f } } };

                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < w; ++j)
                    {
                        XMVECTOR value = inPixels[j];

                        if (XMVector3NearEqual(value, colorKeyValue, s_tolerance))
                        {
                            value = g_XMZero;
                        }
                        else
                        {
                            value = XMVectorSelect(g_XMOne, value, g_XMSelect1110);
                        }

                        outPixels[j] = value;
                    }
                }, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [colorkey] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Invert Y Channel --------------------------------------------------------
        if (dwOptions & (DWORD64(1) << OPT_INVERT_Y))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
                {
                    static const XMVECTORU32 s_selecty = { { { XM_SELECT_0, XM_SELECT_1, XM_SELECT_0, XM_SELECT_0 } } };

                    UNREFERENCED_PARAMETER(y);

                    for (size_t j = 0; j < w; ++j)
                    {
                        XMVECTOR value = inPixels[j];

                        XMVECTOR inverty = XMVectorSubtract(g_XMOne, value);

                        outPixels[j] = XMVectorSelect(value, inverty, s_selecty);
                    }
                }, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [inverty] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Reconstruct Z Channel ---------------------------------------------------
        if (dwOptions & (DWORD64(1) << OPT_RECONSTRUCT_Z))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = TransformImage(image->GetImages(), image->GetImageCount(), image->GetMetadata(),
                [&](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t w, size_t y)
            {
                static const XMVECTORU32 s_selectz = { { { XM_SELECT_0, XM_SELECT_0, XM_SELECT_1, XM_SELECT_0 } } };

                UNREFERENCED_PARAMETER(y);

                for (size_t j = 0; j < w; ++j)
                {
                    XMVECTOR value = inPixels[j];

                    XMVECTOR z = XMVectorSqrt(XMVectorSubtract(g_XMOne, XMVector2Dot(value, value)));

                    outPixels[j] = XMVectorSelect(value, z, s_selectz);
                }
            }, *timage);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [reconstructz] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Determine whether preserve alpha coverage is required (if requested) ----
        if (preserveAlphaCoverageRef > 0.0f && HasAlpha(info.format) && !image->IsAlphaAllOpaque())
        {
            preserveAlphaCoverage = true;
        }

        // --- Generate mips -----------------------------------------------------------
        TEX_FILTER_FLAGS dwFilter3D = dwFilter;
        if (!ispow2(info.width) || !ispow2(info.height) || !ispow2(info.depth))
        {
            if (!tMips || info.mipLevels != 1)
            {
                nonpow2warn = true;
            }

            if (info.dimension == TEX_DIMENSION_TEXTURE3D)
            {
                // Must force triangle filter for non-power-of-2 volume textures to get correct results
                dwFilter3D = TEX_FILTER_TRIANGLE;
            }
        }

        if ((!tMips || info.mipLevels != tMips || preserveAlphaCoverage) && (info.mipLevels != 1))
        {
            // Mips generation only works on a single base image, so strip off existing mip levels
            // Also required for preserve alpha coverage so that existing mips are regenerated

            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            TexMetadata mdata = info;
            mdata.mipLevels = 1;
            hr = timage->Initialize(mdata);
            if (FAILED(hr))
            {
                wprintf(L" FAILED [copy to single level] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            if (info.dimension == TEX_DIMENSION_TEXTURE3D)
            {
                for (size_t d = 0; d < info.depth; ++d)
                {
                    hr = CopyRectangle(*image->GetImage(0, 0, d), Rect(0, 0, info.width, info.height),
                        *timage->GetImage(0, 0, d), TEX_FILTER_DEFAULT, 0, 0);
                    if (FAILED(hr))
                    {
                        wprintf(L" FAILED [copy to single level] (%x)\n", static_cast<unsigned int>(hr));
                        return 1;
                    }
                }
            }
            else
            {
                for (size_t i = 0; i < info.arraySize; ++i)
                {
                    hr = CopyRectangle(*image->GetImage(0, i, 0), Rect(0, 0, info.width, info.height),
                        *timage->GetImage(0, i, 0), TEX_FILTER_DEFAULT, 0, 0);
                    if (FAILED(hr))
                    {
                        wprintf(L" FAILED [copy to single level] (%x)\n", static_cast<unsigned int>(hr));
                        return 1;
                    }
                }
            }

            image.swap(timage);
            info.mipLevels = image->GetMetadata().mipLevels;

            if (cimage && (tMips == 1))
            {
                // Special case for trimming mips off compressed images and keeping the original compressed highest level mip
                mdata = cimage->GetMetadata();
                mdata.mipLevels = 1;
                hr = timage->Initialize(mdata);
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [copy compressed to single level] (%x)\n", static_cast<unsigned int>(hr));
                    return 1;
                }

                if (mdata.dimension == TEX_DIMENSION_TEXTURE3D)
                {
                    for (size_t d = 0; d < mdata.depth; ++d)
                    {
                        auto simg = cimage->GetImage(0, 0, d);
                        auto dimg = timage->GetImage(0, 0, d);

                        memcpy_s(dimg->pixels, dimg->slicePitch, simg->pixels, simg->slicePitch);
                    }
                }
                else
                {
                    for (size_t i = 0; i < mdata.arraySize; ++i)
                    {
                        auto simg = cimage->GetImage(0, i, 0);
                        auto dimg = timage->GetImage(0, i, 0);

                        memcpy_s(dimg->pixels, dimg->slicePitch, simg->pixels, simg->slicePitch);
                    }
                }

                cimage.swap(timage);
            }
            else
            {
                cimage.reset();
            }
        }

        if ((!tMips || info.mipLevels != tMips) && (info.width > 1 || info.height > 1 || info.depth > 1))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            if (info.dimension == TEX_DIMENSION_TEXTURE3D)
            {
                hr = GenerateMipMaps3D(image->GetImages(), image->GetImageCount(), image->GetMetadata(), dwFilter3D | dwFilterOpts, tMips, *timage);
            }
            else
            {
                hr = GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), dwFilter | dwFilterOpts, tMips, *timage);
            }
            if (FAILED(hr))
            {
                wprintf(L" FAILED [mipmaps] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            auto& tinfo = timage->GetMetadata();
            info.mipLevels = tinfo.mipLevels;

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.format == tinfo.format);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Preserve mipmap alpha coverage (if requested) ---------------------------
        if (preserveAlphaCoverage && info.mipLevels != 1 && (info.dimension != TEX_DIMENSION_TEXTURE3D))
        {
            std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
            if (!timage)
            {
                wprintf(L"\nERROR: Memory allocation failed\n");
                return 1;
            }

            hr = timage->Initialize(image->GetMetadata());
            if (FAILED(hr))
            {
                wprintf(L" FAILED [keepcoverage] (%x)\n", static_cast<unsigned int>(hr));
                return 1;
            }

            const size_t items = image->GetMetadata().arraySize;
            for (size_t item = 0; item < items; ++item)
            {
                auto img = image->GetImage(0, item, 0);
                assert(img);

                hr = ScaleMipMapsAlphaForCoverage(img, info.mipLevels, info, item, preserveAlphaCoverageRef, *timage);
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [keepcoverage] (%x)\n", static_cast<unsigned int>(hr));
                    return 1;
                }
            }

#ifndef NDEBUG
            auto& tinfo = timage->GetMetadata();
#endif

            assert(info.width == tinfo.width);
            assert(info.height == tinfo.height);
            assert(info.depth == tinfo.depth);
            assert(info.arraySize == tinfo.arraySize);
            assert(info.mipLevels == tinfo.mipLevels);
            assert(info.miscFlags == tinfo.miscFlags);
            assert(info.dimension == tinfo.dimension);

            image.swap(timage);
            cimage.reset();
        }

        // --- Premultiplied alpha (if requested) --------------------------------------
        if ((dwOptions & (DWORD64(1) << OPT_PREMUL_ALPHA))
            && HasAlpha(info.format)
            && info.format != DXGI_FORMAT_A8_UNORM)
        {
            if (info.IsPMAlpha())
            {
                printf("\nWARNING: Image is already using premultiplied alpha\n");
            }
            else
            {
                auto img = image->GetImage(0, 0, 0);
                assert(img);
                size_t nimg = image->GetImageCount();

                std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
                if (!timage)
                {
                    wprintf(L"\nERROR: Memory allocation failed\n");
                    return 1;
                }

                hr = PremultiplyAlpha(img, nimg, info, TEX_PMALPHA_DEFAULT | dwSRGB, *timage);
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [premultiply alpha] (%x)\n", static_cast<unsigned int>(hr));
                    continue;
                }

                auto& tinfo = timage->GetMetadata();
                info.miscFlags2 = tinfo.miscFlags2;

                assert(info.width == tinfo.width);
                assert(info.height == tinfo.height);
                assert(info.depth == tinfo.depth);
                assert(info.arraySize == tinfo.arraySize);
                assert(info.mipLevels == tinfo.mipLevels);
                assert(info.miscFlags == tinfo.miscFlags);
                assert(info.dimension == tinfo.dimension);

                image.swap(timage);
                cimage.reset();
            }
        }

        // --- Compress ----------------------------------------------------------------
        if (IsCompressed(tformat) && (FileType == CODEC_DDS))
        {
            if (cimage && (cimage->GetMetadata().format == tformat))
            {
                // We never changed the image and it was already compressed in our desired format, use original data
                image.reset(cimage.release());

                auto& tinfo = image->GetMetadata();

                if ((tinfo.width % 4) != 0 || (tinfo.height % 4) != 0)
                {
                    non4bc = true;
                }

                info.format = tinfo.format;
                assert(info.width == tinfo.width);
                assert(info.height == tinfo.height);
                assert(info.depth == tinfo.depth);
                assert(info.arraySize == tinfo.arraySize);
                assert(info.mipLevels == tinfo.mipLevels);
                assert(info.miscFlags == tinfo.miscFlags);
                assert(info.dimension == tinfo.dimension);
            }
            else
            {
                cimage.reset();

                auto img = image->GetImage(0, 0, 0);
                assert(img);
                size_t nimg = image->GetImageCount();

                std::unique_ptr<ScratchImage> timage(new (std::nothrow) ScratchImage);
                if (!timage)
                {
                    wprintf(L"\nERROR: Memory allocation failed\n");
                    return 1;
                }

                bool bc6hbc7 = false;
                switch (tformat)
                {
                case DXGI_FORMAT_BC6H_TYPELESS:
                case DXGI_FORMAT_BC6H_UF16:
                case DXGI_FORMAT_BC6H_SF16:
                case DXGI_FORMAT_BC7_TYPELESS:
                case DXGI_FORMAT_BC7_UNORM:
                case DXGI_FORMAT_BC7_UNORM_SRGB:
                    bc6hbc7 = true;

                    {
                        static bool s_tryonce = false;

                        if (!s_tryonce)
                        {
                            s_tryonce = true;

                            if (!(dwOptions & (DWORD64(1) << OPT_NOGPU)))
                            {
                                if (!CreateDevice(adapter, pDevice.GetAddressOf()))
                                    wprintf(L"\nWARNING: DirectCompute is not available, using BC6H / BC7 CPU codec\n");
                            }
                            else
                            {
                                wprintf(L"\nWARNING: using BC6H / BC7 CPU codec\n");
                            }
                        }
                    }
                    break;

                default:
                    break;
                }

                TEX_COMPRESS_FLAGS cflags = dwCompress;
#ifdef _OPENMP
                if (!(dwOptions & (DWORD64(1) << OPT_FORCE_SINGLEPROC)))
                {
                    cflags |= TEX_COMPRESS_PARALLEL;
                }
#endif

                if ((img->width % 4) != 0 || (img->height % 4) != 0)
                {
                    non4bc = true;
                }

                if (bc6hbc7 && pDevice)
                {
                    hr = Compress(pDevice.Get(), img, nimg, info, tformat, dwCompress | dwSRGB, alphaWeight, *timage);
                }
                else
                {
                    hr = Compress(img, nimg, info, tformat, cflags | dwSRGB, alphaThreshold, *timage);
                }
                if (FAILED(hr))
                {
                    wprintf(L" FAILED [compress] (%x)\n", static_cast<unsigned int>(hr));
                    continue;
                }

                auto& tinfo = timage->GetMetadata();

                info.format = tinfo.format;
                assert(info.width == tinfo.width);
                assert(info.height == tinfo.height);
                assert(info.depth == tinfo.depth);
                assert(info.arraySize == tinfo.arraySize);
                assert(info.mipLevels == tinfo.mipLevels);
                assert(info.miscFlags == tinfo.miscFlags);
                assert(info.dimension == tinfo.dimension);

                image.swap(timage);
            }
        }
        else
        {
            cimage.reset();
        }

        // --- Set alpha mode ----------------------------------------------------------
        if (HasAlpha(info.format)
            && info.format != DXGI_FORMAT_A8_UNORM)
        {
            if (image->IsAlphaAllOpaque())
            {
                info.SetAlphaMode(TEX_ALPHA_MODE_OPAQUE);
            }
            else if (info.IsPMAlpha())
            {
                // Aleady set TEX_ALPHA_MODE_PREMULTIPLIED
            }
            else if (dwOptions & (DWORD64(1) << OPT_SEPALPHA))
            {
                info.SetAlphaMode(TEX_ALPHA_MODE_CUSTOM);
            }
            else if (info.GetAlphaMode() == TEX_ALPHA_MODE_UNKNOWN)
            {
                info.SetAlphaMode(TEX_ALPHA_MODE_STRAIGHT);
            }
        }
        else
        {
            info.SetAlphaMode(TEX_ALPHA_MODE_UNKNOWN);
        }

        // --- Save result -------------------------------------------------------------
        {
            auto img = image->GetImage(0, 0, 0);
            assert(img);
            size_t nimg = image->GetImageCount();

            PrintInfo(info);
            wprintf(L"\n");

            // Figure out dest filename

            
                DDS_FLAGS ddsFlags = DDS_FLAGS_NONE;
                if (dwOptions & (DWORD64(1) << OPT_USE_DX10))
                {
                    ddsFlags |= DDS_FLAGS_FORCE_DX10_EXT | DDS_FLAGS_FORCE_DX10_EXT_MISC2;
                }
                else if (dwOptions & (DWORD64(1) << OPT_USE_DX9))
                {
                    ddsFlags |= DDS_FLAGS_FORCE_DX9_LEGACY;
                }

                //perform analysis
                //hr = SaveToDDSFile(img, nimg, info, ddsFlags, szDest);
                texdiag::AnalyzeBCData data;
                hr = texdiag::AnalyzeBC(*img, data);
                if (FAILED(hr))
                {
//                    wprintf(L"ERROR: Failed analyzing BC image at slice %3zu, mip %3zu (%08X)\n", slice, mip, static_cast<unsigned int>(hr));
                    wprintf(L"ERROR: Failed analyzing BC image at slice\n");
                    return 1;
                }

                data.Print(img->format);


            

            if (FAILED(hr))
            {
                wprintf(L" FAILED (%x)\n", static_cast<unsigned int>(hr));
                continue;
            }
            wprintf(L"\n");
        }
    }




    if (dwOptions & (DWORD64(1) << OPT_TIMING))
    {
        LARGE_INTEGER qpcEnd;
        if (QueryPerformanceCounter(&qpcEnd))
        {
            LONGLONG delta = qpcEnd.QuadPart - qpcStart.QuadPart;
            wprintf(L"\n Processing time: %f seconds\n", double(delta) / double(qpcFreq.QuadPart));
        }
    }

    return 0;
}
