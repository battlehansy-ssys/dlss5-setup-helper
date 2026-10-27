// DLSS 5 설치 도우미 — 단일 실행 파일, 런타임 의존성 없음
// 빌드: build.bat  (cl /MT 정적 링크)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <urlmon.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "\
                       "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ================================================================ 설정
// 버전 규칙: 아래 OPTI_URL(내려받기 주소)을 바꿀 때마다 맨 뒷자리를 1 올린다.
static const wchar_t* APP_VER  = L"1.0.3.4";
static const wchar_t* OPTI_VER = L"F5-DLSSNR v0.1.33";
// 이 판을 만들 때 전수조사에 쓴 드라이버. 창 위에 같이 적어서
// 받으신 분이 자기 것과 바로 견줄 수 있게 한다.
// 드라이버가 달라서 생긴 일을 우리 탓으로 알고 제보하시는 경우가 잦다. (2026-10-24)
static const wchar_t* TESTED_DRV = L"621.01";
static const wchar_t* OPTI_URL =
    L"https://github.com/janblade/OptiScaler-F5-DLSSNR-Multipass/releases/download/v0.1.33/OptiScaler-DLSSNR-F5-0.1.33-with-dlss-fg.zip";
static const wchar_t* SWAPPER_URL = L"https://github.com/rakanki911/DLSS5-Swapper";
static const wchar_t* NEXUS_URL   = L"https://www.nexusmods.com/site/mods/2224?tab=files";
static const wchar_t* BACKUP_DIR  = L"_DLSS5_backup";
static const wchar_t* MANIFEST    = L"_DLSS5_installed.txt";
static const wchar_t* DATA_DIR    = L"data";                  // 사용자 눈에 안 띄게 한 곳에 모은다
static const wchar_t* OPTI_ZIP    = L"optiscaler.dat";        // .zip 이면 사람들이 풀어 본다
static const wchar_t* FEED_DIR    = L"feeder";               // 프로그램 옆 Feeder 재료 폴더
static const wchar_t* FEED_ADDON  = L"dlss5-feed.addon64";
static const wchar_t* FEED_HOST   = L"dlss5-feed-host64.exe";
static const wchar_t* FEED_FX     = L"DLSS5_Feed.fx";
static const wchar_t* FEED_CFG    = L"dlss5-feed.cfg";
static const wchar_t* RS_PRESET   = L"ReShadePreset.ini";
static const wchar_t* DLSS_DLL    = L"nvngx_dlss.dll";
static const wchar_t* RENODX_ADDON= L"renodx-dlss5.addon64";
static const wchar_t* RS_DIR      = L"reshade";              // 프로그램 옆 ReShade 재료 폴더
static const wchar_t* RS_DLL      = L"ReShade64.dll";
static const wchar_t* RS_INI      = L"ReShade.ini";
static const wchar_t* RS_SHADERS  = L"reshade-shaders";
static const wchar_t* NR_DLL      = L"nvngx_dlssnr.dll";
// 카드 세대별 신경망 파일. 프로그램 옆에 이 이름으로 같이 넣어 배포한다.
// 신경망 파일은 하나만 쓴다. 두 판을 시험한 결과 이 판이 모든 경우에 동작했다.
static const wchar_t* LOG_FILE    = L"DLSS5_log.txt";
// 창에 보여 주는 진행 기록의 한도. 파일에는 전부 남으므로 창만 제한한다.
// EDIT 컨트롤은 한도를 안 정하면 32,767자에서 막혀 그 뒤가 안 찍힌다.
static const int LOG_UI_MAX  = 1000000;   // 창에 담을 최대 글자수
static const int LOG_UI_TRIM = 200000;    // 넘치면 앞에서 이만큼 잘라낸다

// 설치할 때 ini에 써 넣는 값
struct IniSet { const wchar_t* section; const wchar_t* key; const wchar_t* value; };
static const IniSet PRESET[] = {
    // 켜는 데 꼭 필요한 것만 건드린다. 화질 관련 값은 OptiScaler 기본값 그대로 둔다.
    { L"Upscalers", L"Dx11Upscaler",   L"dlss_12" },
    { L"Upscalers", L"Dx12Upscaler",   L"dlss"    },
    { L"Upscalers", L"VulkanUpscaler", L"dlss"    },
    // 2026-10-22. 포크가 켤 때마다 깃허브를 보고 "새 판 있음" 줄을 띄운다.
    // 받아 가신 분이 그 링크로 직접 올리면 우리가 시험하지 않은 판이 들어가고,
    // ini 항목 이름이 바뀌면 우리 기본값이 통째로 안 먹는다.
    // 새 판은 우리가 시험해서 올려 드리는 쪽으로 한다.
    { L"Hotfix",    L"CheckForUpdate",  L"false"   },
    { L"DlssNr",    L"Enabled",        L"true"    },
    { L"DlssNr",    L"ToggleKey",      L"0x76"    },

    // 2026-10-22. 기본값을 "Natural 1패스" 로 바꿨다.
    //
    // 1.0.2.1 까지는 값을 하나씩 손으로 맞춰 넣었다. 그런데 사람이 창에서
    // 프리셋 단추를 한 번 누르면 그 값들이 전부 덮어써진다. 손으로 맞춘 값은
    // 클릭 한 번에 사라진다. 그래서 프리셋 한 벌을 통째로 넣는다.
    // 안내도 "창에서 프리셋만 바꿔 보세요" 한 줄로 끝난다.
    //
    // 아래는 창의 [Natural] 단추가 실제로 ini 에 쓰는 값이다. 추측이 아니라
    // 레데리2에서 눌러 저장한 뒤 ini 를 읽어 그대로 옮긴 것이다.
    //
    // 생드니 실측 (4K, RTX 5070 Ti, Vulkan, 리세이드 끈 상태)
    //   바닐라 60fps -> 57fps.
    //   자갈이 하나씩 떨어지고 철제 난간 살이 갈라진다. 관공서 석재에 결이 선다.
    //   간판 글자는 안 깨진다 (프리셋 다섯 개 전부 확인했다).
    //   2패스는 40fps 라 기본으로 두지 않는다. 창에서 올릴 수 있다.
    { L"DlssNr",    L"RunBeforeSR",    L"true" },
    { L"DlssNr",    L"Passes",         L"1"    },
    { L"DlssNr",    L"Style",          L"1"    },   // 0 standard, 1 natural, 2 cinematic
    { L"DlssNr",    L"ReversibleMode", L"5"    },
    { L"DlssNr",    L"Transfer",       L"2"    },
    { L"DlssNr",    L"Intensity",      L"1.4"  },
    { L"DlssNr",    L"LocalStructure", L"1.4"  },
    { L"DlssNr",    L"LocalTone",      L"1.4"  },

    // WorkingScale 은 반드시 써 넣는다. 1.0.2.1 이 0.5 를 박아 놓았기 때문이다.
    // 안 쓰면 전에 깐 분들 ini 에 0.5 가 그대로 남는다.
    // 0.5 는 모델을 화면의 반에서 돌리고 그 결과를 늘린다. 풍경 잔결이 거기서 사라진다.
    // 2026-10-21 레데리2 실측: 풀밭 -6.2%, 화면 전체 -4.1%. 손해만 보던 값이다.
    { L"DlssNr",    L"WorkingScale",   L"1.0"  },

    // ColourStrength 도 반드시 써 넣는다. 1.0.2.1 이 0.0 을 박아 놓았다.
    // Natural 은 이 값을 기본(1.0)으로 쓴다. 안 쓰면 새로 까는 분은 1.0,
    // 전에 깐 분은 0.0 이 되어 같은 설치 도구인데 화면이 달라진다.
    //
    // 주의 - 0.0 은 원래 지글거림을 잡으려고 넣었던 값이다 (2026-10-02).
    // 킹덤컴2 밤 성벽과 스파이더맨 2 에서 하늘처럼 평탄한 면에 색 알갱이가
    // 뿌려졌고, 이 값을 0 으로 내려서 잡았다. 1.0 으로 돌리면 돌아올 수 있다.
    // 킹덤컴2 는 전수조사 대상이다. 밤 성벽을 반드시 볼 것.
    // 돌아오면 이 줄만 0.0 으로 내린다. 색만 빠지고 결은 그대로다.
    { L"DlssNr",    L"ColourStrength", L"1.0"  },

    // 2패스, 3패스로 올렸을 때 쓸 값. 1패스에서는 쓰이지 않는다.
    // 안 쓰면 2패스가 Natural 이 의도한 모습이 아니라 1패스를 그대로 물려받은
    // 모습이 된다 (local tone 만 0). 사용자가 올려 볼 수 있어야 하므로 같이 넣는다.
    { L"DlssNr",    L"Pass2Style",          L"1"    },
    { L"DlssNr",    L"Pass2Intensity",      L"1.0"  },
    { L"DlssNr",    L"Pass2LocalStructure", L"1.0"  },
    { L"DlssNr",    L"Pass2LocalTone",      L"1.0"  },
    { L"DlssNr",    L"Pass2SkinStructure",  L"-1.0" },
    { L"DlssNr",    L"Pass2AutoMask",       L"true" },
    { L"DlssNr",    L"Pass3Style",          L"1"    },
    { L"DlssNr",    L"Pass3Intensity",      L"0.75" },
    { L"DlssNr",    L"Pass3LocalStructure", L"1.5"  },
    { L"DlssNr",    L"Pass3LocalTone",      L"0.46" },
    { L"DlssNr",    L"Pass3SkinStructure",  L"1.0"  },
    { L"DlssNr",    L"Pass3AutoMask",       L"true" },
};

// 게임이 읽어 들일 수 있는 이름들 (앞에서부터 시도)
static const wchar_t* PROXY_NAMES[] = {
    L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll", L"wininet.dll", L"winhttp.dll"
};

// ================================================================ 단축키
//
// 2026-10-27. 창에서 고를 수 있게 했다. 전에는 네 개를 코드에 박아 두었다.
// 아세토코르사 에보에서 "조작이 먹통" 이라는 제보가 왔는데, 리세이드 창이
// 열려 있는 동안 키보드와 마우스를 리세이드가 가져가기 때문이었다.
// 창 여는 키를 숫자 8 로 박아 둬서, 숫자 키를 쓰는 게임이면 모르고 누르게 된다.
// (리세이드 원래 기본값은 Home 이라 그럴 일이 적다)
//
// 고른 값 하나를 파일 네 군데에 서로 다른 모양으로 적어야 한다.
//   ReShade.ini   [INPUT] KeyOverlay / KeyEffects   10진수 + 수식키 셋  "56,0,0,0"
//   ReShade.ini   [RenoDX.DLSS5] NRToggleKey        10진수             "118"
//   OptiScaler.ini ShortcutKey / ToggleKey           16진수             "0x76"
struct KeyChoice { const wchar_t* name; int vk; };
static const KeyChoice KEY_LIST[] = {
    { L"Home",        0x24 }, { L"Insert",      0x2D }, { L"Delete",      0x2E },
    { L"End",         0x23 }, { L"Page Up",     0x21 }, { L"Page Down",   0x22 },
    { L"Scroll Lock", 0x91 }, { L"Pause",       0x13 }, { L"물결표 `",     0xC0 },
    { L"F1",  0x70 }, { L"F2",  0x71 }, { L"F3",  0x72 }, { L"F4",  0x73 },
    { L"F5",  0x74 }, { L"F6",  0x75 }, { L"F7",  0x76 }, { L"F8",  0x77 },
    { L"F9",  0x78 }, { L"F10", 0x79 }, { L"F11", 0x7A }, { L"F12", 0x7B },
    { L"숫자 0", 0x30 }, { L"숫자 1", 0x31 }, { L"숫자 2", 0x32 }, { L"숫자 3", 0x33 },
    { L"숫자 4", 0x34 }, { L"숫자 5", 0x35 }, { L"숫자 6", 0x36 }, { L"숫자 7", 0x37 },
    { L"숫자 8", 0x38 }, { L"숫자 9", 0x39 },
    // 영문 글자는 맨 아래에 둔다. 게임이 쓰고 있을 확률이 제일 높아서
    // 위로 올리면 모르고 고르기 쉽다.
    { L"A", 0x41 }, { L"B", 0x42 }, { L"C", 0x43 }, { L"D", 0x44 }, { L"E", 0x45 },
    { L"F", 0x46 }, { L"G", 0x47 }, { L"H", 0x48 }, { L"I", 0x49 }, { L"J", 0x4A },
    { L"K", 0x4B }, { L"L", 0x4C }, { L"M", 0x4D }, { L"N", 0x4E }, { L"O", 0x4F },
    { L"P", 0x50 }, { L"Q", 0x51 }, { L"R", 0x52 }, { L"S", 0x53 }, { L"T", 0x54 },
    { L"U", 0x55 }, { L"V", 0x56 }, { L"W", 0x57 }, { L"X", 0x58 }, { L"Y", 0x59 },
    { L"Z", 0x5A },
};
static const int KEY_COUNT = (int)(sizeof(KEY_LIST) / sizeof(KEY_LIST[0]));

// 칸 네 개. 순서를 바꾸면 아래 코드가 전부 어긋난다.
enum { HK_RS_OVERLAY = 0, HK_RS_EFFECTS, HK_DLSS_PANEL, HK_DLSS_TOGGLE, HK_N };
static const wchar_t* HK_LABEL[HK_N] = {
    L"리세이드 창", L"효과 켜기/끄기", L"DLSS 5 창", L"DLSS 5 켜기/끄기"
};
// 아무것도 안 적혀 있을 때 쓰는 값. 지금까지 코드에 박혀 있던 것과 같다.
static const int HK_DEFAULT[HK_N] = { 0x38, 0x78, 0x2D, 0x76 };   // 숫자8 · F9 · Insert · F7

// ================================================================ 전역
static HWND g_hWnd, g_hList, g_hLog, g_hInstall, g_hRestore, g_hPick, g_hCopy, g_hStatus, g_hGpu, g_hApiLbl, g_hApi, g_hModeLbl, g_hMode;
static HWND g_hRs;   // 리세이드도 같이 넣을지 고르는 칸 (기본 꺼짐)
static HWND g_hKeyLbl[HK_N], g_hKeyBox[HK_N];
// 칸에 띄울 때 읽어 온 값. 설치할 때 이것과 다를 때만 파일에 쓴다.
// 안 바꾸시면 아무 파일도 안 건드린다.
static int g_hkLoaded[HK_N];
// 대표님이 그 칸을 직접 건드리셨나. 한 번 고르신 칸은 게임을 바꿔도 안 건드린다.
// 안 건드리신 칸만 그 게임의 지금 값을 보여 준다.
static bool g_hkTouched[HK_N];
// 창 쪽 함수인데 설치 코드가 먼저 나와서 미리 알려 둔다.
static int HotKeyVk(int which);
static int VkToRow(int vk);
static void ApplyHotKeys(const std::wstring& gameDir, bool modeB);
// 지금 고른 키의 이름. 진행 기록에 "숫자 8" 을 박아 두면 키를 바꾸셨을 때
// 글과 실제가 어긋난다. 2026-10-27 붉은사막에서 9 로 넣고 8 이라고 적었다.
static std::wstring HotKeyName(int which);
static HWND g_hTitle, g_hDesc;
static HWND g_hVer;   // 오른쪽 아래 판 번호. 제목줄만으로는 확인이 어렵다 (2026-10-27)
static HFONT g_font, g_fontTitle;
struct Game { std::wstring name; std::wstring dir; };
static std::vector<Game> g_games;
static volatile LONG g_busy = 0;
static std::wstring g_logPath;
static int g_tier = 0;   // 2 = RTX 50, 1 = RTX 20/30/40, 0 = 그 외
static bool g_gpuNvidia = false;   // 엔비디아 카드인가
static int  g_gpuSeries = 0;       // RTX 뒤의 숫자. 5070 이면 5
static std::wstring g_gpuName;     // 화면에 보여줄 이름
// 라데온이나 옛 카드가 없어도 안내 창을 확인할 수 있게 한다.
//   DLSS5_Setup.exe /gpu=amd     엔비디아가 아닌 것처럼
//   DLSS5_Setup.exe /gpu=rtx30   RTX 30 인 것처럼
static std::wstring g_gpuFake;

#define WM_LOG    (WM_APP+1)
#define WM_STATUS (WM_APP+2)
#define WM_DONE   (WM_APP+3)

// 게임을 고를 때마다 어느 길로 갈지 미리 살피는데, 그때까지 진행 기록에 남으면
// 사용자가 아직 누르지도 않은 일이 적힌다. 그 동안만 기록을 잠근다. (2026-10-23)
static bool g_logMuted = false;
static void Log(const std::wstring& s)    { if (g_logMuted) { return; }
                                            PostMessageW(g_hWnd, WM_LOG,    0, (LPARAM)_wcsdup(s.c_str())); }
static void Status(const std::wstring& s) { PostMessageW(g_hWnd, WM_STATUS, 0, (LPARAM)_wcsdup(s.c_str())); }

// ================================================================ 파일 유틸
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring TempDir() {
    wchar_t buf[MAX_PATH]; GetTempPathW(MAX_PATH, buf);
    std::wstring d = std::wstring(buf) + L"dlss5_setup\\";
    CreateDirectoryW(d.c_str(), NULL);
    return d;
}
static void RemoveTree(const std::wstring& dir) {
    if (!DirExists(dir)) return;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring p = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
        else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir.c_str());
}
static std::string ReadFileBytes(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    DWORD sz = GetFileSize(h, NULL), rd = 0;
    std::string s; s.resize(sz);
    ReadFile(h, &s[0], sz, &rd, NULL); CloseHandle(h);
    s.resize(rd); return s;
}
static bool WriteFileBytes(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0; BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, NULL);
    CloseHandle(h); return ok == TRUE;
}
static void AppendFileBytes(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; WriteFile(h, data.data(), (DWORD)data.size(), &wr, NULL);
    CloseHandle(h);
}
static std::string W2U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring LowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s;
}

static std::string LowerA(std::string s) {
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
    return s;
}
static std::wstring SelfDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p = buf;
    size_t s = p.find_last_of(L'\\');
    return (s == std::wstring::npos) ? L"." : p.substr(0, s);
}
static std::wstring NowStamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t b[32];
    swprintf_s(b, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    return b;
}

// 하위 폴더까지 만든다. 백업 자리를 게임 폴더와 같은 모양으로 맞추는 데 쓴다.
static void MakeDirs(const std::wstring& dir) {
    for (size_t i = 3; i < dir.size(); i++) {
        if (dir[i] == L'\\') {
            CreateDirectoryW(dir.substr(0, i).c_str(), NULL);
        }
    }
    CreateDirectoryW(dir.c_str(), NULL);
}
// 두 파일의 내용이 같은지. 크기가 다르면 바로 아니다.
static bool SameFileContent(const std::wstring& a, const std::wstring& b) {
    HANDLE ha = CreateFileW(a.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, 0, NULL);
    if (ha == INVALID_HANDLE_VALUE) { return false; }
    HANDLE hb = CreateFileW(b.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, 0, NULL);
    if (hb == INVALID_HANDLE_VALUE) { CloseHandle(ha); return false; }
    LARGE_INTEGER sa, sb;
    bool same = GetFileSizeEx(ha, &sa) && GetFileSizeEx(hb, &sb)
             && sa.QuadPart == sb.QuadPart;
    while (same) {
        char ba[65536], bb[65536];
        DWORD ra = 0, rb = 0;
        if (!ReadFile(ha, ba, sizeof(ba), &ra, NULL)) { same = false; break; }
        if (!ReadFile(hb, bb, sizeof(bb), &rb, NULL)) { same = false; break; }
        if (ra != rb) { same = false; break; }
        if (ra == 0) { break; }
        if (memcmp(ba, bb, ra) != 0) { same = false; break; }
    }
    CloseHandle(ha); CloseHandle(hb);
    return same;
}

// bakRoot 를 넘기면, 덮어쓸 자리에 있던 파일을 먼저 그 폴더로 옮긴다.
//
// 2026-10-25. 전에는 백업을 이름 세 개(OptiScaler.ini, nvngx_dlss.dll, 프록시)
// 에만 떴다. 그런데 여기서 게임 폴더에 91개를 쏟아붓는다. 그 안에는
// README.md · LICENSE · SHA256SUMS.txt · docs 폴더 · redist 폴더처럼
// 게임도 가지고 있을 법한 이름이 있다. 겹치면 백업 없이 덮였고,
// 설치 기록에는 적혔다. 되돌리기는 백업이 없으면 우리가 만든 것으로 보고
// 지우므로, 게임 파일이 영영 사라진다.
// 대표님 PC 열세 게임에서는 안 겹쳤지만, 수백 개 게임에 깔리면 반드시 나온다.
//
// 되돌리기 쪽은 고칠 것이 없다. 이미 backup\<같은 상대 경로> 를 찾아
// 되살린다. 여기서 같은 모양으로 넣어 주기만 하면 된다.
static void CopyTree(const std::wstring& from, const std::wstring& to,
                     std::vector<std::wstring>& added, const std::wstring& rel = L"",
                     const std::wstring& bakRoot = L"") {
    CreateDirectoryW(to.c_str(), NULL);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring src = from + L"\\" + n, dst = to + L"\\" + n;
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyTree(src, dst, added, r, bakRoot);
        } else {
            if (!bakRoot.empty() && FileExists(dst)) {
                std::wstring bak = bakRoot + L"\\" + r;
                // 이미 백업돼 있으면 그대로 둔다. 첫 백업이 진짜 원본이다.
                // 지금 넣으려는 것과 똑같은 파일이면 보관할 것이 없다.
                // (되돌리기 없이 두 번 설치하신 경우. 우리가 지난번에 넣은 것이다)
                if (!FileExists(bak) && !SameFileContent(dst, src)) {
                    MakeDirs(bak.substr(0, bak.rfind(L'\\')));
                    if (!CopyFileW(dst.c_str(), bak.c_str(), TRUE)) {
                        // 백업을 못 떴으면 덮지 않는다. 되돌릴 수 없는 것을 만들지 않는다.
                        // 조용히 넘기지 않는다. 안 넣은 파일이 있다는 것을 적어 둔다.
                        wchar_t wb[320];
                        swprintf_s(wb, L"  [경고] 원래 파일을 백업하지 못해 넣지 않았습니다 (윈도우 오류 %lu): ",
                                   GetLastError());
                        Log(std::wstring(wb) + r);
                        continue;
                    }
                }
            }
            if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) { added.push_back(r); }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static bool FindFileDeep(const std::wstring& root, const std::wstring& name, int depth, std::wstring& out) {
    if (depth < 0 || !DirExists(root)) return false;
    std::wstring direct = root + L"\\" + name;
    if (FileExists(direct)) { out = direct; return true; }
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        std::wstring ln = LowerW(n);
        if (ln == L"windows" || ln == L"$recycle.bin" || ln == L"system volume information") continue;
        if (FindFileDeep(root + L"\\" + n, name, depth - 1, out)) { found = true; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
static bool RunHidden(const std::wstring& cmd, const std::wstring& workdir) {
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    std::wstring c = cmd;
    if (!CreateProcessW(NULL, &c[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        workdir.empty() ? NULL : workdir.c_str(), &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code == 0;
}

// 이 파일이 OptiScaler인지 확인 (제품명으로 판별)
static bool IsOptiScalerFile(const std::wstring& path);
static bool IsReShadeFile(const std::wstring& path);

// 되돌리기가 끝난 뒤, 우리가 넣었던 것이 정말로 사라졌는지 확인한다.
// 사람이 로그를 읽지 않아도 되도록 남은 것을 직접 찾아서 돌려준다.
// 되돌리기에서 지우지 않는 설정 파일인가. (.ini / .cfg)
// ReShade.ini, ReShadePreset.ini, dlss5-feed.cfg, OptiScaler.ini 에는 사용자가 게임 안에서 바꾼 값이 저장된다.
static bool IsSettingsFile(const std::wstring& rel) {
    std::wstring l = LowerW(rel);
    if (l.size() >= 4 && l.compare(l.size() - 4, 4, L".ini") == 0) return true;
    if (l.size() >= 4 && l.compare(l.size() - 4, 4, L".cfg") == 0) return true;
    return false;
}

// 되돌리기 뒤에 남은 것을 치운다. 치우지 못한 것만 돌려준다.
//
// 2026-10-18. 전에는 남은 것을 찾기만 하고 "직접 지우셔도 됩니다" 라고 띄웠다.
// 그런데 여기서 보는 것은 전부 우리가 넣은 것이고, 프록시 dll 은
// IsOptiScalerFile 로 우리 것임을 확인까지 한 뒤다. 확인했으면 그냥 지우면 된다.
// 창은 "게임이 켜져 있어 잠긴" 경우에만 의미가 있다.
//
// 9월 판으로 깔았던 분들은 그때 설치 기록에 OptiScaler 폴더와 Licenses 폴더가
// 안 적혀 있어서 되돌려도 그 두 개가 남는다. 그분들이 이 창을 보게 된다.
// _DLSS5_backup 은 손대지 않는다. 사용자의 원래 파일이 들어 있던 자리다.
// 하위 폴더에 우리 파일이 남아 있는지 본다.
//
// 2026-10-18 프래그마타. 실행 파일이 _storage_ 안에 있는 게임이라 거기 설치되는데,
// 점검은 게임 폴더 바로 아래만 봤다. 9월 판이 설치 기록 없이 넣은
// dxgi.dll(리세이드)과 winmm.dll(DLSS 5)이 그대로 남아 있었는데
// "넣었던 것이 모두 사라졌습니다" 라고 나왔다.
// 우리 파일인지 확인하고 지운다. 남의 것은 건드리지 않는다.
static void SweepOursDeep(const std::wstring& dir, int depth, int* swept, bool isRoot) {
    if (depth < 0) return;
    const wchar_t* names[] = { L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll",
                               L"wininet.dll", L"winhttp.dll", L"d3d11.dll", L"d3d12.dll",
                               L"opengl32.dll", L"d3d9.dll" };
    for (int i = 0; i < 10; i++) {
        std::wstring p = dir + L"\\" + names[i];
        if (!FileExists(p)) { continue; }
        if (!IsOptiScalerFile(p) && !IsReShadeFile(p)) { continue; }
        if (DeleteFileW(p.c_str())) { (*swept)++; }
    }

    // 2026-10-18 프래그마타. dll 만 지웠더니 _storage_ 안에 피더가 혼자 남아
    // 게임이 켜자마자 꺼졌다. dlss5-feed-crash.dmp 가 그때 생겼다.
    // 1.0.1.2 에서 안 터진 이유는 그때는 하위 폴더를 아예 안 봤기 때문이다.
    // 짝을 맞춰 같이 치운다. 이름이 분명히 우리 것인 파일만 지운다.
    const wchar_t* ours[] = { L"dlss5-feed.addon64", L"dlss5-feed.addon32",
                              L"dlss5-feed.cfg", L"dlss5-feed.log",
                              L"dlss5-feed-crash.dmp",
                              L"renodx-dlss5.addon64", L"nvngx.dll_dlssnr.dll",
                              L"nvngx_dlssnr.dll", L"OptiScaler.dll",
                              L"_DLSS5_installed.txt" };
    for (int i = 0; i < 10; i++) {
        // 2026-10-20. 게임 폴더 바로 아래의 dlss5-feed.cfg 는 사용자가 게임 안에서
        // 맞춰 놓은 값이다 (작업 해상도 등). 되돌리기 메시지도 "남겨 둔다"고 적는데
        // 여기서 지우고 있었다. 말과 동작이 달랐다.
        // 하위 폴더에 있는 것은 짝 잃은 잔해이므로 그대로 치운다 (프래그마타 사고).
        if (isRoot && _wcsicmp(ours[i], L"dlss5-feed.cfg") == 0) { continue; }
        std::wstring p = dir + L"\\" + ours[i];
        if (!FileExists(p)) { continue; }
        if (DeleteFileW(p.c_str())) { (*swept)++; }
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring ln = LowerW(n);
        if (ln == L"reshade-shaders" || ln == L"_dlss5_backup") continue;
        SweepOursDeep(dir + L"\\" + n, depth - 1, swept, false);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// 빈 폴더만 재귀로 치운다. 파일이 하나라도 있으면 그 폴더는 남는다.
// 되돌린 뒤 reshade-shaders 밑에 빈 폴더가 백 개 넘게 남는 것을 보고 넣었다
// (2026-10-22 레데리2). RemoveDirectoryW 는 비어 있을 때만 성공한다.
static int PruneEmptyDirs(const std::wstring& dir) {
    int gone = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") {
                continue;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                gone += PruneEmptyDirs(dir + L"\\" + n);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (RemoveDirectoryW(dir.c_str())) {
        gone++;
    }
    return gone;
}

// 우리가 만든 ReShadePreset.ini 인가.
//
// 켠 효과가 우리 것뿐이면 우리 프리셋이다. 되돌릴 때 통째로 지운다.
// 사용자가 쓰시던 프리셋에는 다른 효과가 들어 있다. 그건 파일을 두고
// 우리가 켠 것만 뺀다 (DisableFeedTechniques).
// 2026-10-22 라오어 1에서 예전 판이 남긴 것을 발견해 넣었다.
//
// 2026-10-24. 두 가지를 고쳤다.
//   1) 켜 주는 효과에 AdaptiveSharpen 과 FilmGrain2 가 늘었다. 목록에 넣는다.
//   2) "파일이 2048바이트보다 크면 사용자 것" 과 "값이 적혀 있으면 사용자 것"
//      이라는 두 판단을 뺐다. 이제 우리도 값을 적어 넣고, 게임을 한 번 켜면
//      리세이드가 TechniqueSorting 에 셰이더 이름 전부를 적는다. 16KB 가 넘는다.
//      그 뒤로는 우리 프리셋도 영영 우리 것으로 안 보였다.
//
// 켠 효과(Techniques) 만 본다. TechniqueSorting 은 순서 목록이라 보지 않는다.
static bool IsOurReShadePreset(const std::wstring& path) {
    std::string t = ReadFileBytes(path);
    if (t.empty()) {
        return false;
    }
    const char* ours[] = { "DH_UBER_MOTION_020@dh_uber_motion.fx",
                           "DLSS5_Feed@DLSS5_Feed.fx",
                           "AdaptiveSharpen@AdaptiveSharpen.fx",
                           "FilmGrain2@FilmGrain2.fx" };
    // AdaptiveSharpen 과 FilmGrain2 는 흔히 쓰는 효과다. 그 둘만 켜 둔 프리셋은
    // 우리 것일 수도 있고 원래 쓰시던 것일 수도 있어서 가릴 수가 없다.
    // 우리가 만든 것이면 설치 기록에 적혀 있고 그 기록대로 지워진다.
    // 여기서는 우리라는 표시가 분명할 때만 참이라고 한다. (2026-10-24)
    bool marked = (t.find("DLSS5_Feed@DLSS5_Feed.fx") != std::string::npos)
               || (t.find("DH_UBER_MOTION_020@dh_uber_motion.fx") != std::string::npos);
    if (!marked) {
        return false;
    }
    size_t p = 0;
    bool seen = false;
    while (p < t.size()) {
        size_t e = t.find('\n', p);
        std::string ln = t.substr(p, (e == std::string::npos ? t.size() : e) - p);
        p = (e == std::string::npos) ? t.size() : e + 1;
        while (!ln.empty() && (ln[ln.size() - 1] == '\r' || ln[ln.size() - 1] == ' ')) {
            ln.erase(ln.size() - 1);
        }
        if (ln.compare(0, 11, "Techniques=") != 0) {
            continue;
        }
        seen = true;
        std::string rest = ln.substr(11);
        size_t q = 0;
        while (q <= rest.size()) {
            size_t c = rest.find(',', q);
            std::string one = (c == std::string::npos) ? rest.substr(q) : rest.substr(q, c - q);
            if (!one.empty()) {
                bool mine = false;
                for (int k = 0; k < 4; k++) {
                    if (one == ours[k]) { mine = true; }
                }
                if (!mine) {
                    return false;
                }
            }
            if (c == std::string::npos) {
                break;
            }
            q = c + 1;
        }
    }
    return seen;
}

static bool IsOurDlssRuntime(const std::wstring& path);
static bool IsREFramework(const std::wstring& path);
static bool DisableFeedTechniques(const std::wstring& presetPath);

static int CheckRevertLeftovers(const std::wstring& gameDir, std::wstring& reportOut) {
    std::wstring NL(1, (wchar_t)10);
    std::wstring bad;
    int miss = 0;
    int swept = 0;

    // 우리가 넣는 파일들. 하나라도 남아 있으면 되돌리기가 덜 된 것이다.
    // 설정 파일(OptiScaler.ini, dlss5-feed.cfg)은 일부러 남기므로 여기서 보지 않는다.
    const wchar_t* files[] = {
        L"nvngx_dlssnr.dll", L"nvngx.dll_dlssnr.dll",
        L"dlss5-feed.addon64", L"dlss5-feed.addon32",
        L"renodx-dlss5.addon64",
        L"OptiScaler.dll",
        L"setup_windows.bat", L"setup_linux.sh",
        L"READ ME - DLSS Neural Rendering.txt",
        L"!! EXTRACT ALL FILES TO GAME FOLDER !!",
        L"_DLSS5_installed.txt",
        // 2026-10-22. janblade 포크 zip 이 게임 폴더에 같이 푸는 파일들이다.
        // 흔한 이름(README.md, LICENSE)은 게임 것일 수 있어 넣지 않는다.
        L"SHA256SUMS.txt", L"INSTALL-DLSSNR.md", L"TROUBLESHOOTING.md",
        L"get_streamline.ps1",
    };
    for (size_t i = 0; i < _countof(files); i++) {
        std::wstring p = gameDir + L"\\" + files[i];
        if (!FileExists(p)) continue;
        DeleteFileW(p.c_str());
        if (FileExists(p)) { bad += std::wstring(L"  - ") + files[i] + NL; miss++; }
        else swept++;
    }

    // RenoDX 애드온이 올라올 때 만드는 설정 백업을 치운다.
    //   ReShade.ini.renodx-dlss5-pre-v4-migration-20261023-162148.bak
    // 날짜가 붙어 이름이 매번 달라서 목록으로는 못 잡는다. 앞머리로 찾는다.
    // 2026-10-23 바이오하자드 RE4 에서 되돌린 뒤 남아 있는 것을 보고 넣었다.
    {
        WIN32_FIND_DATAW fd;
        std::wstring pat = gameDir + L"\\ReShade.ini.renodx-dlss5-*.bak";
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { continue; }
                std::wstring p = gameDir + L"\\" + fd.cFileName;
                DeleteFileW(p.c_str());
                if (FileExists(p)) {
                    bad += std::wstring(L"  - ") + fd.cFileName + NL;
                    miss++;
                } else {
                    swept++;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    // 설치할 때 꺼 둔 REFramework 을 원래 이름으로 돌려놓는다.
    // 우리가 붙인 이름이지만, 혹시 모르니 내용까지 보고 돌려놓는다.
    {
        std::wstring di  = gameDir + L"\\dinput8.dll";
        std::wstring off = gameDir + L"\\dinput8.dll.off";
        if (FileExists(off) && !FileExists(di) && IsREFramework(off)) {
            if (MoveFileW(off.c_str(), di.c_str())) {
                swept++;
            } else {
                bad += std::wstring(L"  - dinput8.dll.off 를 되돌리지 못했습니다") + NL;
                miss++;
            }
        }
    }

    // nvngx_dlss.dll 은 여기서 지우지 않는다.
    //
    // 2026-10-23. 한 번 "우리 것이면 지운다" 로 넣었다가 빼냈다. 레데리2 의 게임 폴더에
    // 있는 파일도, 백업 폴더에 있는 파일도 우리 것과 크기·해시가 완전히 같다.
    //   58956400  C85F971CE023C9F3...  우리 것
    //   58956400  C85F971CE023C9F3...  레데리2 게임 폴더
    //   58956400  C85F971CE023C9F3...  레데리2 백업 폴더
    // 그 규칙대로면 되돌리기가 둘 다 지워서 레데리2 의 DLSS 가 통째로 날아간다.
    // 우리가 이번 설치에서 넣은 것이면 설치 기록에 적혀 있고, 그 기록을 따라
    // 이미 지워진다. 기록에 없는 것은 건드리지 않는다.
    // 남아 있더라도 설치할 때 IsOurDlssRuntime 으로 걸러내므로 길이 뒤집히지는 않는다.

    // 폴더. _DLSS5_backup 은 사용자의 원래 파일 자리라 손대지 않는다.
    // 2026-10-22. docs 와 redist 는 janblade 포크가 새로 넣는 폴더다.
    // 매니페스트로 파일은 지워지지만 빈 폴더가 남는다.
    // 2026-10-23. dlss5 는 RenoDX 애드온이 만드는 폴더다 (dlss5_log, state).
    // 애드온이 없으면 쓸모가 없고, 다시 깔면 다시 만들어진다.
    //
    // reshade-cache 는 일부러 안 지운다. 구워 둔 셰이더라 지우면 다시 깔 때마다
    // 886개를 또 굽는다(23초). 남겨 두면 2초면 끝난다. 리세이드가 안 맞는 것은
    // 알아서 버리므로 묵은 캐시가 문제를 일으키지 않는다.
    const wchar_t* dirs[] = { L"OptiScaler", L"Licenses", L"host64", L"docs", L"redist",
                              L"dlss5" };
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        std::wstring p = gameDir + L"\\" + dirs[i];
        if (!DirExists(p)) continue;
        RemoveTree(p);
        if (DirExists(p)) { bad += std::wstring(L"  - ") + dirs[i] + L" 폴더" + NL; miss++; }
        else swept++;
    }
    // 2026-10-22. reshade-shaders 는 매니페스트로 파일만 지워져 빈 폴더가 남는다.
    // RemoveDirectoryW 는 비어 있을 때만 성공하므로, 사용자가 넣어 둔 셰이더가
    // 있으면 저절로 실패하고 그대로 남는다. 안쪽 폴더부터 지운다.
    {
        std::wstring sh = gameDir + L"\\reshade-shaders";
        if (DirExists(sh)) {
            swept += PruneEmptyDirs(sh);
        }
    }

    // 우리가 만든 프리셋이면 치운다.
    // 사용자 것이면 파일은 그대로 두고, 우리가 켠 효과 둘만 뺀다. (2026-10-23 매드맥스)
    {
        std::wstring pr = gameDir + L"\\ReShadePreset.ini";
        if (FileExists(pr)) {
            if (IsOurReShadePreset(pr)) {
                DeleteFileW(pr.c_str());
                if (!FileExists(pr)) {
                    swept++;
                }
            } else if (DisableFeedTechniques(pr)) {
                swept++;
            }
        }
    }

    if (DirExists(gameDir + L"\\_DLSS5_backup"))
        { bad += L"  - _DLSS5_backup 폴더" + NL; miss++; }

    // 프록시로 쓰인 dll 이 아직 OptiScaler 인지. 우리 것이 맞으면 지운다.
    const wchar_t* px[] = { L"winmm.dll", L"dxgi.dll", L"version.dll", L"dbghelp.dll",
                            L"wininet.dll", L"winhttp.dll" };
    for (int i = 0; i < 6; i++) {
        std::wstring p = gameDir + L"\\" + px[i];
        if (!IsOptiScalerFile(p)) continue;
        DeleteFileW(p.c_str());
        if (IsOptiScalerFile(p))
            { bad += std::wstring(L"  - ") + px[i] + L" (아직 OptiScaler 입니다)" + NL; miss++; }
        else swept++;
    }

    // Feeder 셰이더
    {
        std::wstring p = gameDir + L"\\reshade-shaders\\Shaders\\DLSS5_Feed.fx";
        if (FileExists(p)) {
            DeleteFileW(p.c_str());
            if (FileExists(p)) { bad += L"  - DLSS5_Feed.fx" + NL; miss++; }
            else swept++;
        }
    }

    // 하위 폴더까지 본다. 옛 판이 거기 넣어둔 것이 있을 수 있다.
    SweepOursDeep(gameDir, 3, &swept, true);

    if (swept > 0) {
        wchar_t sb[160];
        swprintf_s(sb, L"  남아 있던 것 %d개를 치웠습니다.", swept);
        Log(sb);
    }

    reportOut = bad;
    return miss;
}
// 프로그램 옆의 자료 파일이 온전한지 확인한다.
// 옛 폴더로 실행하거나 복사가 덜 된 채로 설치하면 설치는 되는데 게임에서 안 켜진다.
// 900개를 다 보면 느리므로 핵심 파일만 확인한다. 기본은 크기로 보고,
// 크기가 같은데 내용이 다른 판이 실제로 돌아다니는 신경망 모델만 CRC 까지 본다.
// (923928A8 판은 크기가 똑같지만 RTX 40 과 RenoDX 에서 동작하지 않는다)
static long long FileSizeOf(const std::wstring& p);
static std::wstring Crc32File(const std::wstring& p);
struct NeedFile { const wchar_t* rel; long long size; const wchar_t* what; const wchar_t* crc; };
static const NeedFile NEEDED[] = {
    { L"data\\optiscaler.dat",                  143731279, L"OptiScaler 본체",      NULL },
    { L"data\\nvngx_dlssnr.dll",                165840496, L"신경망 모델",          L"9C56B352" },
    { L"data\\feeder\\nvngx_dlss.dll",          58956400, L"DLSS 런타임",          NULL },
    { L"data\\feeder\\dlss5-feed.addon64",        297472, L"DLSS5-Feeder",         NULL },
    { L"data\\feeder\\renodx-dlss5.addon64",     3186176, L"RenoDX 애드온",        NULL },
    { L"data\\feeder\\host64\\dlss5-feed-host64.exe", 146944, L"Feeder 보조 실행 파일", NULL },
    { L"data\\feeder\\shaders\\DLSS5_Feed.fx",          51193, L"Feeder 셰이더",    NULL },
    { L"data\\feeder\\dlss5-feed.cfg",               329, L"Feeder 설정",          NULL },
    { L"data\\feeder\\ReShadePreset.ini",            190, L"리세이드 프리셋",      NULL },
    { L"data\\reshade\\ReShade64.dll",           5592064, L"리세이드",             NULL },
    { L"data\\reshade\\ReShade.ini",                 316, L"리세이드 설정",        NULL },
    // look_values.ini 는 일부러 넣지 않는다. 이 표는 크기가 한 바이트만 달라도
    // "압축을 다시 푸세요" 창을 띄운다. 그런데 그 파일은 **고치시라고 넣은 파일**이다.
    // 사용자가 그레인 값을 0.05 로 바꾸는 순간 크기가 달라져 그 창이 뜬다.
    // 한글판과 영문판은 주석이 달라 길이도 애초에 같을 수 없다.
    // 파일이 없거나 깨져 있으면 효과는 켜지되 셰이더 기본값으로 돈다 (2892줄 참고).
};

// 빠지거나 크기가 다른 항목 수를 돌려준다.
static int CheckOwnFiles(std::wstring& reportOut) {
    std::wstring NL(1, (wchar_t)10);
    std::wstring bad;
    int miss = 0;
    for (int i = 0; i < (int)(sizeof(NEEDED) / sizeof(NEEDED[0])); i++) {
        std::wstring p = SelfDir() + L"\\" + NEEDED[i].rel;
        long long sz = FileSizeOf(p);
        if (sz < 0) {
            bad += std::wstring(L"  - ") + NEEDED[i].what + L" 가 없습니다  (" + NEEDED[i].rel + L")" + NL;
            miss++;
        } else if (sz != NEEDED[i].size) {
            wchar_t b[320];
            swprintf_s(b, L"  - %s 의 크기가 다릅니다 (%lld 이어야 하는데 %lld)",
                       NEEDED[i].what, NEEDED[i].size, sz);
            bad += std::wstring(b) + NL;
            miss++;
        } else if (NEEDED[i].crc) {
            std::wstring c = Crc32File(p);
            if (c != NEEDED[i].crc) {
                bad += std::wstring(L"  - ") + NEEDED[i].what + L" 가 다른 판입니다 (CRC " + c +
                       L", " + NEEDED[i].crc + L" 이어야 함)" + NL;
                miss++;
            }
        }
    }
    reportOut = bad;
    return miss;
}
// 이 폴더에서 실행 중인 프로그램이 있는지 본다.
// 게임이 켜져 있으면 dll 이 잠겨서 지우거나 덮어쓸 수 없다.
static bool AnyProcessRunningIn(const std::wstring& dir, std::wstring& whichOut) {
    std::wstring want = LowerW(dir);
    if (!want.empty() && want[want.size()-1] != L'\\') want += L"\\";
    DWORD pids[2048], needed = 0;
    if (!EnumProcesses(pids, sizeof(pids), &needed)) return false;
    int count = (int)(needed / sizeof(DWORD));
    for (int i = 0; i < count; i++) {
        if (pids[i] == 0 || pids[i] == GetCurrentProcessId()) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        wchar_t path[MAX_PATH] = {}; DWORD sz = MAX_PATH;
        BOOL ok = QueryFullProcessImageNameW(h, 0, path, &sz);
        CloseHandle(h);
        if (!ok) continue;
        std::wstring lp = LowerW(path);
        if (lp.compare(0, want.size(), want) == 0) {
            whichOut = path;
            return true;
        }
    }
    return false;
}

// 지우고 나서 실제로 사라졌는지 확인한다. 잠겨 있으면 실패를 돌려준다.
static bool DeleteFileChecked(const std::wstring& p) {
    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(p.c_str())) return true;
    return !FileExists(p);   // 이미 없으면 성공으로 본다
}
// 파일의 ProductName 을 읽는다 (못 읽으면 빈 문자열)
static std::wstring ProductNameOf(const std::wstring& path) {
    if (!FileExists(path)) return L"";
    DWORD dummy = 0;
    DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (sz == 0) return L"";
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return L"";
    struct LangCp { WORD lang, cp; } *lc = NULL;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lc, &len) || len < 4) return L"";
    wchar_t sub[128];
    swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\ProductName", lc->lang, lc->cp);
    wchar_t* val = NULL; UINT vlen = 0;
    if (!VerQueryValueW(buf.data(), sub, (LPVOID*)&val, &vlen) || !val) return L"";
    return LowerW(val);
}
// 32비트 실행 파일인가.
//
// 2026-10-24 톰레이더(2013)에서 하루를 썼다. 그 게임은 32비트인데 우리가
// 64비트 ReShade64.dll 만 넣었다. 64비트 dll 은 32비트 프로세스에 들어갈 수
// 없어서 윈도우가 그냥 안 읽는다. ReShade.log 조차 안 생긴다.
// 그런데 기록에는 "설치 완료" 가 찍힌다. 조용히 아무 일도 안 일어난다.
// 자리를 dxgi 에서 d3d11 로 바꿔 가며 두 번 헛짓했다. 자리 문제가 아니었다.
//
// PE 머리의 Machine 값 두 바이트만 보면 된다. 게임을 켜지 않아도 알 수 있다.
//   0x8664 = x64,  0x014C = x86
static bool ExeIs32Bit(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { return false; }
    BYTE head[0x400];
    DWORD got = 0;
    BOOL ok = ReadFile(h, head, sizeof(head), &got, NULL);
    CloseHandle(h);
    if (!ok || got < 0x40) { return false; }
    if (head[0] != 'M' || head[1] != 'Z') { return false; }
    DWORD e = *(DWORD*)(head + 0x3C);
    if (e + 6 > got) { return false; }
    if (head[e] != 'P' || head[e + 1] != 'E') { return false; }
    WORD machine = *(WORD*)(head + e + 4);
    return machine == 0x014C;
}

static bool IsOptiScalerFile(const std::wstring& path) {
    return ProductNameOf(path).find(L"optiscaler") != std::wstring::npos;
}
static bool IsReShadeFile(const std::wstring& path) {
    return ProductNameOf(path).find(L"reshade") != std::wstring::npos;
}

// 백업하면 안 되는 파일인가. 우리가 먼저 넣어 둔 것이면 "게임의 원래 파일"이 아니다.
//
// 2026-10-24. @J_s_iny 님 스파이더맨 2 기록에서 찾았다. 이 게임은 원래
// winmm.dll 이 없어서 첫 설치 때 백업할 것이 없었다. 백업 폴더에 winmm.dll 이
// 안 생겼으니, 두 번째 설치 때는 "아직 백업 안 됐다"로 보고 우리가 넣어 둔
// OptiScaler 를 게임의 원래 파일로 알고 백업했다 (25863680 바이트).
//   [15:39:40]   백업함: winmm.dll
//
// 되돌리기 자체는 깨끗하게 끝난다. CheckRevertLeftovers 가 프록시 자리를
// 다시 훑어서 아직 OptiScaler 면 지운다. 문제는 설치 중 자리를 옮길 때다.
// 옛 winmm 을 지우고 백업본을 제자리에 돌려놓는데, 그 백업본이 OptiScaler 라
// winmm 과 dxgi 에 두 개가 생긴다.
//
// OptiScaler.ini 는 가리지 않는다. 직접 OptiScaler 를 쓰시던 분의 설정이
// 들어 있을 수 있어서, 우리 것일 때 한 번 더 백업되는 쪽이 덜 위험하다.
static bool IsOurBackupCandidate(const std::wstring& path, const std::wstring& name) {
    std::wstring n = LowerW(name);
    if (n == L"optiscaler.ini") { return false; }
    if (n == L"nvngx_dlss.dll") { return IsOurDlssRuntime(path); }
    return IsOptiScalerFile(path) || IsReShadeFile(path);
}

// 파일 안에 이 ASCII 문자열이 있는지. 리세이드 애드온 지원 빌드 판별에 쓴다.
static bool FileContainsAscii(const std::wstring& p, const char* needle) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t nl = strlen(needle);
    std::vector<char> buf(1 << 20);
    std::string tail; DWORD got = 0; bool found = false;
    while (!found && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail + std::string(&buf[0], got);
        if (chunk.find(needle) != std::string::npos) found = true;
        tail = (chunk.size() > nl) ? chunk.substr(chunk.size() - nl) : chunk;
    }
    CloseHandle(h);
    return found;
}
// 애드온을 받아 주는 리세이드인가 (애드온 미지원 빌드에는 이 심볼이 없다)
static bool IsReShadeAddonBuild(const std::wstring& p) {
    return FileContainsAscii(p, "ReShadeRegisterAddon");
}
// 이 폴더에 이미 다른 프로그램이 물려 있는지 검사한다.
// 겹쳐 넣으면 게임이 아예 안 켜지므로, 하나라도 걸리면 설치하지 않는다.
static std::vector<std::wstring> CollectConflicts(const std::wstring& dir) {
    // 리세이드나 다른 용도의 모드는 건드리지 않는다. 같이 써도 문제없다.
    // DLSS 5를 두 번 거는 것만 막는다. (RenoDX / Feeder 애드온)
    std::vector<std::wstring> found;
    const wchar_t* addons[] = { L"renodx-dlss5.addon64", L"renodx-dlss5.addon32",
                                L"dlss5-feed.addon64", L"dlss5-feed.addon32" };
    for (int i = 0; i < 4; i++)
        if (FileExists(dir + L"\\" + addons[i])) found.push_back(addons[i]);
    return found;
}

// ================================================================ 그래픽카드 인식
struct GpuInfo { std::wstring name, driver, nvVersion; bool isNvidia = false; bool blackwell = false; int tier = 0; int series = 0; };

static std::wstring RegStr(HKEY root, const wchar_t* sub, const wchar_t* name) {
    HKEY k; if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[512] = {}; DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(k, name, NULL, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    return (r == ERROR_SUCCESS) ? std::wstring(buf) : L"";
}
// 32.0.16.1664 → 616.64 (엔비디아 표기)
static std::wstring NvidiaVersionFrom(const std::wstring& driver) {
    std::wstring digits;
    for (size_t i = 0; i < driver.size(); i++) if (iswdigit(driver[i])) digits += driver[i];
    if (digits.size() < 5) return L"";
    std::wstring last5 = digits.substr(digits.size() - 5);
    return last5.substr(0, 3) + L"." + last5.substr(3);
}
static GpuInfo DetectGpu() {
    GpuInfo g;
    const wchar_t* base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (int i = 0; i < 8; i++) {
        wchar_t sub[320];
        swprintf_s(sub, L"%s\\%04d", base, i);
        std::wstring desc = RegStr(HKEY_LOCAL_MACHINE, sub, L"DriverDesc");
        if (desc.empty()) continue;
        std::wstring ver = RegStr(HKEY_LOCAL_MACHINE, sub, L"DriverVersion");
        std::wstring low = LowerW(desc);
        if (low.find(L"nvidia") != std::wstring::npos) {
            g.isNvidia = true; g.name = desc; g.driver = ver;
            g.nvVersion = NvidiaVersionFrom(ver);
            size_t p = low.find(L"rtx ");
            if (p != std::wstring::npos && low.size() > p + 4) {
                wchar_t gen = low[p + 4];
                if (gen >= L'2' && gen <= L'9') g.series = gen - L'0';
                if (gen == L'5') { g.blackwell = true; g.tier = 2; }
                else if (gen == L'4' || gen == L'3' || gen == L'2') { g.tier = 1; }
            }
            return g;
        }
        if (g.name.empty()) { g.name = desc; g.driver = ver; }
    }
    return g;
}

// ================================================================ 신경망 파일 위치 기억
static std::wstring ConfigPath() {
    wchar_t buf[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) return L"";
    std::wstring d = std::wstring(buf) + L"\\DLSS5Setup";
    CreateDirectoryW(d.c_str(), NULL);
    return d + L"\\neural_path.txt";
}
static std::wstring LoadSavedNr() {
    std::wstring c = ConfigPath();
    if (c.empty()) return L"";
    std::wstring p = U2W(ReadFileBytes(c));
    while (!p.empty() && (p.back() == L'\r' || p.back() == L'\n' || p.back() == L' ')) p.pop_back();
    return FileExists(p) ? p : L"";
}
static void SaveNrPath(const std::wstring& p) {
    std::wstring c = ConfigPath();
    if (!c.empty()) WriteFileBytes(c, W2U(p));
}

// ================================================================ 스팀 게임 찾기
static std::string VdfValue(const std::string& text, size_t from, const char* key, size_t* posOut) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = text.find(pat, from);
    if (p == std::string::npos) return "";
    size_t q1 = text.find('"', p + pat.size());
    if (q1 == std::string::npos) return "";
    size_t q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    if (posOut) *posOut = q2;
    return text.substr(q1 + 1, q2 - q1 - 1);
}
static void ScanSteam() {
    g_games.clear();
    std::wstring steam = RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) steam = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) { Log(L"스팀을 찾지 못했습니다."); return; }
    std::replace(steam.begin(), steam.end(), L'/', L'\\');
    Log(L"스팀 설치 위치: " + steam);

    std::vector<std::wstring> libs;
    libs.push_back(steam);
    std::string vdf = ReadFileBytes(steam + L"\\steamapps\\libraryfolders.vdf");
    size_t pos = 0;
    while (true) {
        size_t end = 0;
        std::string path = VdfValue(vdf, pos, "path", &end);
        if (path.empty()) break;
        pos = end;
        std::wstring w = U2W(path), clean;
        for (size_t i = 0; i < w.size(); i++) {
            clean += w[i];
            if (w[i] == L'\\' && i + 1 < w.size() && w[i + 1] == L'\\') i++;
        }
        libs.push_back(clean);
    }
    std::vector<std::wstring> uniq;
    for (size_t i = 0; i < libs.size(); i++) {
        std::wstring n = libs[i];
        while (!n.empty() && (n.back() == L'\\' || n.back() == L'/')) n.pop_back();
        bool dup = false;
        for (size_t j = 0; j < uniq.size(); j++) if (LowerW(uniq[j]) == LowerW(n)) { dup = true; break; }
        if (!dup) uniq.push_back(n);
    }
    libs.swap(uniq);
    for (size_t i = 0; i < libs.size(); i++) Log(L"게임 보관 폴더: " + libs[i]);

    for (size_t i = 0; i < libs.size(); i++) {
        std::wstring apps = libs[i] + L"\\steamapps";
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((apps + L"\\appmanifest_*.acf").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string acf = ReadFileBytes(apps + L"\\" + fd.cFileName);
            std::string name = VdfValue(acf, 0, "name", NULL);
            std::string idir = VdfValue(acf, 0, "installdir", NULL);
            if (name.empty() || idir.empty()) continue;
            std::wstring dir = apps + L"\\common\\" + U2W(idir);
            if (!DirExists(dir)) continue;
            bool dup = false;
            for (size_t k = 0; k < g_games.size(); k++)
                if (LowerW(g_games[k].dir) == LowerW(dir)) { dup = true; break; }
            if (dup) continue;
            Game g; g.name = U2W(name); g.dir = dir;
            g_games.push_back(g);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// ================================================================ 스토어 감지
static const wchar_t* XBOX_TAG = L"  [Xbox]";
static const wchar_t* GOG_TAG = L"  [GOG]";
static const wchar_t* ROCKSTAR_TAG = L"  [Rockstar]";
static const wchar_t* UBI_TAG = L"  [Ubisoft]";
static const wchar_t* EPIC_TAG = L"  [Epic]";
static const wchar_t* XBOX_FOUND = L"엑스박스 게임 %d개를 찾았습니다.";
static const wchar_t* GOG_FOUND = L"GOG 게임 %d개를 찾았습니다.";
static const wchar_t* ROCKSTAR_FOUND = L"록스타 게임 %d개를 찾았습니다.";
static const wchar_t* UBI_FOUND = L"유비소프트 게임 %d개를 찾았습니다.";
static const wchar_t* EPIC_FOUND = L"에픽 게임 %d개를 찾았습니다.";

// 레지스트리 하위 키 이름을 모두 읽는다. 스토어들이 게임마다 키를 하나씩 만든다.
static std::vector<std::wstring> RegSubKeys(HKEY root, const wchar_t* sub) {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    wchar_t name[512];
    for (DWORD i = 0; ; i++) {
        DWORD n = 512;
        if (RegEnumKeyExW(k, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        out.push_back(name);
    }
    RegCloseKey(k);
    return out;
}

// JSON 에서 "이름": "값" 을 꺼낸다. 에픽 매니페스트를 읽는 용도라 이 정도면 된다.
static std::wstring JsonStr(const std::string& s, const char* key) {
    std::string pat = std::string("\"") + key + "\"" + ":";
    size_t p = s.find(pat);
    if (p == std::string::npos) return L"";
    p = s.find('"', p + pat.size());
    if (p == std::string::npos) return L"";
    size_t q = p + 1;
    std::string v;
    while (q < s.size() && s[q] != '"') {
        if (s[q] == '\\' && q + 1 < s.size()) {
            // JSON 이스케이프. 모르는 것이 오면 역슬래시를 살려 둔다.
            // 버리면 경로가 D:dlss5setupdist 처럼 되어 조용히 실패한다.
            char e = s[q + 1];
            q++;
            if (e == '\\' || e == '\"' || e == '/') v += e;
            else { v += '\\'; v += e; }
        }
        else v += s[q];
        q++;
    }
    return U2W(v);
}

static void AddGame(const std::wstring& name, const std::wstring& dir) {
    if (name.empty() || dir.empty() || !DirExists(dir)) return;
    for (size_t i = 0; i < g_games.size(); i++)
        if (LowerW(g_games[i].dir) == LowerW(dir)) return;
    Game g; g.name = name; g.dir = dir;
    g_games.push_back(g);
}

// 안티치트가 도는 게임인가.
// 이런 게임에 dll 을 끼워 넣으면 치트로 판정돼 계정이 정지될 수 있다.
// 폴더 이름만 보면 충분하다. 안티치트는 자기 폴더를 숨기지 않는다.
// 안티치트가 클라이언트 안에 박혀 있어 파일로는 안 잡히는 온라인 게임들.
// 워든(와우)·VAC(CS2, 도타) 같은 것은 별도 파일이 없어 FindAntiCheat 이 못 찾는다.
// 스토어 스캐너에서 배틀넷을 뺐어도 [폴더에서 고르기]로 지정하면 들어가므로
// 여기서 한 번 더 막는다. 계정 정지는 되돌릴 수 없다.
static std::wstring FindOnlineGame(const std::wstring& dir) {
    struct Mark { const wchar_t* key; const wchar_t* shown; };
    static const Mark marks[] = {
        { L"world of warcraft",  L"월드 오브 워크래프트" },
        { L"wow.exe",            L"월드 오브 워크래프트" },
        { L"wowclassic.exe",     L"월드 오브 워크래프트" },
        { L"overwatch",          L"오버워치" },
        { L"heroes of the storm",L"히어로즈 오브 더 스톰" },
        { L"diablo iv",          L"디아블로 4" },
        { L"counter-strike",     L"카운터 스트라이크" },
        { L"cs2.exe",            L"카운터 스트라이크 2" },
        { L"csgo.exe",           L"카운터 스트라이크" },
        { L"dota 2",             L"도타 2" },
        { L"dota2.exe",          L"도타 2" },
        { L"team fortress 2",    L"팀 포트리스 2" },
        { L"valorant",           L"발로란트" },
        { L"league of legends",  L"리그 오브 레전드" },
        { L"leagueoflegends.exe",L"리그 오브 레전드" },
        { L"lostark",            L"로스트아크" },
        { L"tslgame.exe",        L"배틀그라운드" },
        { L"r5apex.exe",         L"에이펙스 레전드" },
        { L"fortniteclient",     L"포트나이트" },
        { L"destiny2.exe",       L"데스티니 2" },
        { L"rainbowsix",         L"레인보우 식스" },
        // 콜 오브 듀티. Ricochet 은 커널에서 돌면서 게임에 끼어드는 프로그램을
        // 감시하고 신고한다. 2026-10-21 제보: 설치 후 cod.exe 가
        // 0x00001337 (ntdll.dll) 로 튀었다. 그 숫자는 윈도 오류가 아니라
        // 게임이 제 손으로 뱉은 것이다. 폴더 이름이 스토어마다 달라 여러 갈래를 본다.
        { L"call of duty",       L"콜 오브 듀티" },
        { L"callofduty",         L"콜 오브 듀티" },
        { L"cod.exe",            L"콜 오브 듀티" },
        { L"modernwarfare",      L"콜 오브 듀티" },
        { L"modern warfare",     L"콜 오브 듀티" },
        { L"black ops",          L"콜 오브 듀티" },
        { L"blackops",           L"콜 오브 듀티" },
        { L"warzone",            L"콜 오브 듀티 워존" },
        { L"ricochet",           L"콜 오브 듀티" },
        // 혼자 하는 것처럼 보여도 항상 서버에 붙어 있는 게임들.
        // 이야기를 따라가는 게임이라도 커널에서 도는 안티치트가 들어 있고,
        // 계정에 유료 재화가 묶여 있어 정지되면 손해가 크다.
        { L"wuthering waves",    L"명조" },
        { L"명조",               L"명조" },
        { L"genshin impact",     L"원신" },
        { L"genshinimpact.exe",  L"원신" },
        { L"star rail",          L"붕괴 스타레일" },
        { L"starrail.exe",       L"붕괴 스타레일" },
        { L"honkai",             L"붕괴" },
        { L"zenlesszonezero",    L"젠레스 존 제로" },
        { L"nikke",              L"승리의 여신 니케" },
        { L"marvel rivals",      L"마블 라이벌즈" },
        { L"marvelrivals",       L"마블 라이벌즈" },
        { L"delta force",        L"델타 포스" },
        { L"deltaforce",         L"델타 포스" },
        { L"black desert",       L"검은사막" },
        { L"blackdesert",        L"검은사막" },
        { L"maplestory",         L"메이플스토리" },
    };

    // 폴더 경로에 이름이 들어 있는지 먼저 본다
    std::wstring lp = LowerW(dir);
    for (size_t i = 0; i < _countof(marks); i++)
        if (lp.find(marks[i].key) != std::wstring::npos) return marks[i].shown;

    // 폴더 안의 파일 이름도 본다 (한 단계만)
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring hit;
    do {
        std::wstring ln = LowerW(fd.cFileName);
        if (ln == L"." || ln == L"..") continue;
        for (size_t i = 0; i < _countof(marks) && hit.empty(); i++)
            if (ln.find(marks[i].key) != std::wstring::npos) hit = marks[i].shown;
        if (!hit.empty()) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return hit;
}

// 깊이를 5 까지 보는 이유 : 명조의 안티치트는
//   <게임>\Wuthering Waves Game\Client\Binaries\Win64\AntiCheatExpert
// 로 다섯 단계 아래에 있다. 2 단계만 보던 예전 판은 이걸 못 잡았다.
// 대신 에셋이 잘게 쪼개진 게임에서 오래 걸리지 않도록 살펴보는 개수를 제한한다.
static std::wstring FindAntiCheat(const std::wstring& dir, int depth, int* budget = 0) {
    int own = 200000;
    if (!budget) budget = &own;
    if (depth < 0 || *budget <= 0) return L"";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring hit;
    do {
        if (--*budget <= 0) break;
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring ln = LowerW(n);
        if (ln.find(L"easyanticheat") != std::wstring::npos ||
            ln.find(L"battleye") != std::wstring::npos ||
            ln.find(L"beservice") != std::wstring::npos ||
            ln.find(L"anticheat") != std::wstring::npos ||
            // 2026-10-18. 전에는 이름에 "vanguard" 가 들어가면 걸었다.
            // 라이즈 오브 툼레이더의 DLC 파일이 이것 때문에 걸렸다 —
            //   bigfile.dlc.pack.ancientvanguard.000.000.nfo
            // DLC 이름이 Ancient Vanguard 였을 뿐이다. 설치가 아예 막혔다.
            // 발로란트 안티치트의 실제 파일 이름으로 바꾼다.
            // (발로란트 자체는 FindOnlineGame 의 이름 목록에서 따로 막는다)
            ln.find(L"vgk.sys") != std::wstring::npos ||
            ln.find(L"vgc.exe") != std::wstring::npos ||
            ln.find(L"vgtray.exe") != std::wstring::npos ||
            ln.find(L"punkbuster") != std::wstring::npos ||
            // 텐센트 ACE (명조 등). 폴더 이름은 AntiCheatExpert 라 위에서 잡히지만
            // 드라이버와 실행 파일은 이 이름으로 따로 들어 있다.
            ln.find(L"ace-base") != std::wstring::npos ||
            ln.find(L"ace-game") != std::wstring::npos ||
            ln.find(L"ace-guard") != std::wstring::npos ||
            ln.find(L"ace-tray") != std::wstring::npos ||
            ln.find(L"sguard") != std::wstring::npos ||
            // 미호요 커널 드라이버 (원신·스타레일)
            ln.find(L"mhyprot") != std::wstring::npos ||
            // 국내 게임에서 흔한 것들
            ln.find(L"gameguard") != std::wstring::npos ||
            ln.find(L"xigncode") != std::wstring::npos ||
            ln.find(L"xtrap") != std::wstring::npos ||
            ln.find(L"hshield") != std::wstring::npos) {
            hit = n; break;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && depth > 0) {
            hit = FindAntiCheat(dir + L"\\" + n, depth - 1, budget);
            if (!hit.empty()) break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return hit;
}

// 엑스박스 앱 게임. 위치 규칙이 단순해서 드라이브 루트만 보면 된다.
//   <드라이브>BSXboxGamesBS<게임 이름>BSContent
// 바로가기가 실제 폴더를 안 가리켜서 사용자가 직접 찾기 어렵다. 그래서 자동으로 넣는다.
static void ScanXbox() {
    int n = 0;
    wchar_t roots[] = L"CDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; roots[i]; i++) {
        std::wstring base = std::wstring(1, roots[i]) + L":\\XboxGames";
        if (!DirExists(base)) continue;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L".." || name == L"GameSave") continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::wstring content = base + L"\\" + name + L"\\Content";
            if (!DirExists(content)) continue;
            AddGame(name + XBOX_TAG, content);
            n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n) { wchar_t b[128]; swprintf_s(b, XBOX_FOUND, n); Log(b); }
}

static void ScanGog() {
    const wchar_t* base = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games";
    std::vector<std::wstring> ids = RegSubKeys(HKEY_LOCAL_MACHINE, base);
    int n = 0;
    for (size_t i = 0; i < ids.size(); i++) {
        std::wstring key = std::wstring(base) + L"\\" + ids[i];
        std::wstring dir = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"path");
        std::wstring name = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"gameName");
        if (name.empty()) name = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"exeFile");
        if (dir.empty() || !DirExists(dir)) continue;
        AddGame(name + GOG_TAG, dir);
        n++;
    }
    if (n) { wchar_t b[128]; swprintf_s(b, GOG_FOUND, n); Log(b); }
}

static void ScanRockstar() {
    const wchar_t* base = L"SOFTWARE\\WOW6432Node\\Rockstar Games";
    std::vector<std::wstring> ks = RegSubKeys(HKEY_LOCAL_MACHINE, base);
    int n = 0;
    for (size_t i = 0; i < ks.size(); i++) {
        std::wstring key = std::wstring(base) + L"\\" + ks[i];
        // 런처·소셜클럽 같은 부속 프로그램도 같은 자리에 키를 만든다. 게임이 아니다.
        std::wstring lk = LowerW(ks[i]);
        if (lk == L"launcher" || lk == L"steam" || lk == L"epic games store" ||
            lk.find(L"social club") != std::wstring::npos) continue;
        std::wstring dir = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"InstallFolder");
        if (dir.empty()) continue;
        while (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/'))
            dir.erase(dir.size() - 1);
        if (!DirExists(dir)) continue;
        AddGame(ks[i] + ROCKSTAR_TAG, dir);
        n++;
    }
    if (n) { wchar_t b[128]; swprintf_s(b, ROCKSTAR_FOUND, n); Log(b); }
}

static void ScanUbisoft() {
    const wchar_t* base = L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs";
    std::vector<std::wstring> ids = RegSubKeys(HKEY_LOCAL_MACHINE, base);
    int n = 0;
    for (size_t i = 0; i < ids.size(); i++) {
        std::wstring key = std::wstring(base) + L"\\" + ids[i];
        std::wstring dir = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"InstallDir");
        if (dir.empty()) continue;
        std::replace(dir.begin(), dir.end(), L'/', L'\\');
        while (!dir.empty() && dir[dir.size() - 1] == L'\\') dir.erase(dir.size() - 1);
        if (!DirExists(dir)) continue;
        size_t sl = dir.find_last_of(L"\\");
        std::wstring name = (sl == std::wstring::npos) ? dir : dir.substr(sl + 1);
        AddGame(name + UBI_TAG, dir);
        n++;
    }
    if (n) { wchar_t b[128]; swprintf_s(b, UBI_FOUND, n); Log(b); }
}

// 에픽은 게임마다 매니페스트 파일(.item, JSON)을 하나씩 둔다.
static void ScanEpic() {
    std::wstring dir = L"C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    if (!DirExists(dir)) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.item").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int n = 0;
    do {
        std::string js = ReadFileBytes(dir + L"\\" + fd.cFileName);
        if (js.empty()) continue;
        std::wstring loc = JsonStr(js, "InstallLocation");
        std::wstring name = JsonStr(js, "DisplayName");
        if (loc.empty() || !DirExists(loc)) continue;
        if (name.empty()) name = JsonStr(js, "AppName");
        AddGame(name + EPIC_TAG, loc);
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (n) { wchar_t b[128]; swprintf_s(b, EPIC_FOUND, n); Log(b); }
}

// 모든 스토어를 훑는다. 스팀부터 보고 나머지를 덧붙인 뒤 이름순으로 정렬한다.
static void ScanAll() {
    ScanSteam();
    ScanXbox();
    ScanGog();
    ScanRockstar();
    ScanUbisoft();
    ScanEpic();
    std::sort(g_games.begin(), g_games.end(),
              [](const Game& a, const Game& b) { return a.name < b.name; });
}

// ================================================================ 게임 폴더 판단
// 폴더 안에 exe 가 하나라도 있는가.
// 언리얼의 Engine\Binaries\Win64 는 dll 만 들어 있는 미끼 폴더라 이것으로 걸러낸다.
static bool DirHasExe(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

// 2026-10-01 스텔라 블레이드 제보로 고침.
// 언리얼 게임은 <게임>\<프로젝트명>\Binaries\Win64 에 본체 exe 가 있고
// Engine\Binaries\Win64 에는 공용 dll 만 있다.
// 예전에는 먼저 걸리는 폴더를 그냥 반환해서 Engine 이 알파벳 순으로 SB 보다 먼저 잡혔고,
// 그 폴더에는 exe 가 없어 "실행 파일 못 찾음" 으로 설치가 중단됐다.
// 이제는 exe 가 들어 있는 폴더를 먼저 쓰고, Engine 은 다른 후보가 없을 때만 쓴다.
static std::wstring ResolveExeDir(const std::wstring& dir) {
    std::wstring ue = dir + L"\\Binaries\\Win64";
    if (DirExists(ue)) return ue;
    std::wstring engineDir, firstDir;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::wstring sub = dir + L"\\" + n + L"\\Binaries\\Win64";
            if (!DirExists(sub)) continue;
            if (LowerW(n) == L"engine") { if (engineDir.empty()) engineDir = sub; continue; }
            if (DirHasExe(sub)) { FindClose(h); return sub; }
            if (firstDir.empty()) firstDir = sub;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!firstDir.empty()) return firstDir;
    if (!engineDir.empty()) return engineDir;
    return dir;
}
static long long FileSizeOf(const std::wstring& p);
static int GameSupportsDlss(const std::wstring& exePath);

// 런처나 설치 프로그램은 게임 본체가 아니다. 이름으로 걸러낸다.
// 위처3의 setup_redlauncher.exe 가 642MB 라 "가장 큰 exe" 규칙으로는 그게 잡힌다.
// 배너로드는 Watchdog\Watchdog.exe (오류 보고 프로그램) 가 잡혀 엉뚱한 폴더에 설치된 적이 있다.
static bool LooksLikeLauncher(const std::wstring& path) {
    std::wstring n = LowerW(path);
    size_t sl = n.find_last_of(L"\\");
    if (sl != std::wstring::npos) n = n.substr(sl + 1);
    const wchar_t* bad[] = { L"launcher", L"setup", L"unins", L"redist", L"crashreport",
                             L"crashhandler", L"vcredist", L"directx", L"eossdk",
                             L"epicgames", L"activation", L"touchup", L"benchmark",
                             L"watchdog", L"crashpad", L"bugsplat", L"sndrpt", L"crs-",
                             L"reporter", L"codegenerator", L"workshop", L"installermessage" };
    for (size_t i = 0; i < _countof(bad); i++)
        if (n.find(bad[i]) != std::wstring::npos) return true;
    return false;
}

// 게임 본체 exe 로 볼 최소 크기. 이보다 작은 exe 는 런처나 껍데기일 가능성이 크다.
static const long long GAME_EXE_MIN = 10LL * 1024 * 1024;
static std::wstring EngineDllNear(const std::wstring& exe);

// 폴더와 그 아래를 뒤져 게임 실행 파일 후보를 모은다.
static void CollectExes(const std::wstring& dir, int depth, std::vector<std::wstring>& out,
                        long long minSize = GAME_EXE_MIN) {
    if (depth < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring full = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring ln = LowerW(n);
            if (ln == L"_dlss5_backup" || ln == L"reshade-shaders" || ln == L"engine") continue;
            CollectExes(full, depth - 1, out, minSize);
            continue;
        }
        std::wstring ln = LowerW(n);
        if (ln.size() < 5 || ln.compare(ln.size() - 4, 4, L".exe") != 0) continue;
        LARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
        if ((long long)sz.QuadPart < minSize) continue;
        if (LooksLikeLauncher(full)) continue;
        out.push_back(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// 게임 본체 실행 파일을 고른다.
// DLSS(NGX) 가 들어 있는 exe 가 있으면 그것을 먼저 쓴다.
// 위처3처럼 DX11 판과 DX12 판이 같이 있는 게임에서 DLSS 되는 쪽을 정확히 집어낸다.
static std::wstring FindGameExe(const std::wstring& dir) {
    std::vector<std::wstring> cands;
    CollectExes(dir, 3, cands);
    if (!cands.empty()) {
        std::wstring biggest; long long bestSz = -1;
        for (size_t i = 0; i < cands.size(); i++) {
            long long sz = FileSizeOf(cands[i]);
            if (sz > bestSz) { bestSz = sz; biggest = cands[i]; }
        }
        for (size_t i = 0; i < cands.size(); i++) {
            if (GameSupportsDlss(cands[i]) == 1) {
                if (cands[i] != biggest)
                    Log(L"  DLSS 가 들어 있는 실행 파일을 찾았습니다: " + cands[i]);
                return cands[i];
            }
        }
        return biggest;
    }

    // 큰 exe 가 없다. 배너로드(TaleWorlds.Native.dll), 킹덤컴2 처럼
    // exe 는 작은 껍데기이고 게임 본체가 옆의 dll 에 들어 있는 게임이다.
    // 작은 exe 중에서 옆에 게임 본체 dll 이 있는 것을 고른다. DLSS 가 든 쪽을 먼저 쓴다.
    std::vector<std::wstring> shells;   // "small" 은 윈도우 헤더의 매크로라 쓰면 안 된다
    CollectExes(dir, 3, shells, 0);
    std::wstring best, bestEng; long long bestDll = -1;
    for (size_t i = 0; i < shells.size(); i++) {
        std::wstring eng = EngineDllNear(shells[i]);
        if (eng.empty()) continue;
        if (GameSupportsDlss(eng) == 1) { best = shells[i]; bestEng = eng; break; }
        long long s = FileSizeOf(eng);
        if (s > bestDll) { bestDll = s; best = shells[i]; bestEng = eng; }
    }
    if (!best.empty())
        Log(L"  실행 파일은 작고 게임 본체는 dll 에 있습니다: " + bestEng);
    return best;
}

// 이 게임이 DLSS 를 지원하는가.
// 실행 파일 안에 엔비디아 NGX / Streamline 참조가 있는지로 판별한다.
// 사용자가 dll 을 따로 넣어도 실행 파일 내용은 바뀌지 않으므로 속지 않는다.
//   1 = 지원,  0 = 미지원,  -1 = 실행 파일을 못 찾아 판단 불가
static int GameSupportsDlss(const std::wstring& exePath) {
    if (exePath.empty() || !FileExists(exePath)) return -1;
    // 2026-10-18. "DLSS" 를 더했다.
    //   라오어1 tlou-i.exe   NVSDK_NGX 없음, nvngx 없음, DLSS 있음
    //   데이즈곤 DaysGone.exe 셋 다 없음
    // 앞의 둘만 보면 라오어를 DLSS 없는 게임으로 보고 리세이드 길로 보낸다.
    const char* marks[] = { "NVSDK_NGX", "nvngx", "sl.interposer", "DLSS" };
    HANDLE h = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    std::vector<char> buf(1 << 20);
    std::string tail; DWORD got = 0; bool hit = false;
    while (!hit && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail + std::string(&buf[0], got);
        for (int i = 0; i < 4 && !hit; i++) {
            if (chunk.find(marks[i]) != std::string::npos) { hit = true; }
        }
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
    return hit ? 1 : 0;
}

// exe 옆에서 게임 본체로 보이는 가장 큰 dll 을 찾는다. (10MB 이상)
// 엔비디아·물리엔진·플랫폼 SDK·프록시 dll 은 이름 앞부분으로 뺀다.
static std::wstring EngineDllNear(const std::wstring& exe) {
    size_t sl = exe.find_last_of(L"\\");
    if (sl == std::wstring::npos) return L"";
    std::wstring dir = exe.substr(0, sl);
    const wchar_t* skip[] = { L"nvngx", L"sl.", L"optiscaler", L"reshade", L"dxgi", L"winmm",
                              L"version", L"dbghelp", L"dbgcore", L"wininet", L"winhttp", L"physx",
                              L"galaxy", L"eossdk", L"steam", L"amd_", L"gfsdk", L"gfesdk",
                              L"d3dcompiler", L"fmod", L"mono", L"msdia", L"libxess", L"ffx",
                              L"bink", L"cef", L"libcef", L"icu", L"qt5", L"qt6", L"avcodec",
                              L"avformat", L"openvr", L"oo2core", L"granny", L"dstorage",
                              L"embree", L"tbb", L"d3d12core", L"nvidia", L"nvapi" };
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring best; long long bestSz = GAME_EXE_MIN - 1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring ln = LowerW(fd.cFileName);
        bool bad = false;
        for (size_t i = 0; i < _countof(skip) && !bad; i++)
            if (ln.rfind(skip[i], 0) == 0) bad = true;
        if (bad) continue;
        LARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
        if ((long long)sz.QuadPart > bestSz) { bestSz = (long long)sz.QuadPart; best = dir + L"\\" + fd.cFileName; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

// 게임 폴더에 있는 nvngx_dlss.dll 이 게임 것인가, 우리가 넣은 것인가.
//
// 2026-10-23 바이오하자드 RE4 에서 물렸다. 되돌리기 없이 그 위에 덮어 설치하면
// 설치 기록이 새것으로 바뀌면서, 먼젓번에 우리가 넣은 파일이 주인 없는 파일이 된다.
// 그 뒤 되돌리기를 해도 기록에 없으니 안 지워지고, 다음 설치 때 그 파일을
// 게임 것으로 보고 OptiScaler 길로 가 버린다.
// RenoDX 애드온이 있나로 가리던 줄이 있었는데, 되돌리기가 애드온만 지우고
// 이 파일은 남겨서 그 방법으로는 못 가린다.
// 흔적 말고 파일 자체를 우리 것과 대조한다. 크기부터 보고 같을 때만 CRC 를 낸다.
static bool IsOurDlssRuntime(const std::wstring& path) {
    std::wstring mine = SelfDir() + L"\\" + DATA_DIR + L"\\" + FEED_DIR + L"\\" + DLSS_DLL;
    if (!FileExists(path) || !FileExists(mine)) { return false; }
    long long a = FileSizeOf(path);
    long long b = FileSizeOf(mine);
    if (a <= 0 || a != b) { return false; }
    return Crc32File(path) == Crc32File(mine);
}

// 불칸 게임인가.
//
// 리세이드 + RenoDX 길은 불칸에서 안 된다. 레데리2 로 밤을 새우고 안 된다는
// 결론을 낸 자리다. 그래서 "DLSS 런타임이 없으면 리세이드 길" 규칙에서
// 불칸만은 빼 준다. 거기로 보내면 될 길이 없어진다.
//
// 게임 이름 목록만으로는 약해서 실행 파일 안의 vulkan-1.dll 이름도 같이 본다.
// 유니코드로 적힌 이름도 잡으려고 0 바이트를 빼고 소문자로 바꿔서 찾는다.
static bool LooksLikeVulkanGame(const std::wstring& exe) {
    std::wstring l = LowerW(exe);
    if (l.find(L"red dead redemption 2") != std::wstring::npos
     || l.find(L"doom") != std::wstring::npos
     || l.find(L"wolfenstein") != std::wstring::npos
     || l.find(L"no man's sky") != std::wstring::npos) {
        return true;
    }
    HANDLE h = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) { return false; }
    std::vector<char> buf(1 << 20);
    std::string tail;
    DWORD got = 0;
    bool hit = false;
    while (!hit && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail;
        chunk.reserve(tail.size() + got);
        for (DWORD i = 0; i < got; i++) {
            char c = buf[i];
            if (c == 0) { continue; }
            if (c >= 'A' && c <= 'Z') { c = (char)(c - 'A' + 'a'); }
            chunk.push_back(c);
        }
        if (chunk.find("vulkan-1.dll") != std::string::npos) { hit = true; }
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
    return hit;
}

// 실행 파일에 "확실한" DLSS 표식이 있는가.
//
// GameSupportsDlss 는 NVSDK_NGX / nvngx / sl.interposer / DLSS 넷 중 하나만 걸려도 1 이다.
// 그중 "DLSS" 는 가장 약한 표식이다. 엔진이 통로만 들고 있어도 잡힌다.
// 나머지 셋은 엔비디아 라이브러리를 실제로 부르는 코드가 있다는 뜻이라 훨씬 믿을 만하다.
//
// 2026-10-23 실측 (이 PC 의 게임들)
//   섀도우툼레이더  NVSDK_NGX · nvngx · DLSS   진짜 DLSS 게임
//   라오어1        DLSS 만                    (streamline 파일이 따로 있어 그쪽에서 걸린다)
//   바이오하자드RE4 DLSS 만                    엔비디아 앱에서도 "지원되지 않음"
//   매드맥스        없음
// 그래서 런타임이 없다고 리세이드 길로 내려보낼 때는 "DLSS 글자 하나뿐"일 때만 그렇게 한다.
static bool ExeHasStrongDlssMark(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) { return false; }
    const char* strong[] = { "NVSDK_NGX", "nvngx", "sl.interposer" };
    std::vector<char> buf(1 << 20);
    std::string tail;
    DWORD got = 0;
    bool hit = false;
    while (!hit && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail + std::string(&buf[0], got);
        for (int i = 0; i < 3 && !hit; i++) {
            if (chunk.find(strong[i]) != std::string::npos) { hit = true; }
        }
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
    return hit;
}

// 이 dinput8.dll 이 REFramework 인가.
//
// dinput8.dll 은 ASI 로더·스크립트훅 등 다른 모드도 흔히 쓰는 이름이다.
// 이름만 보고 치우면 남의 모드를 멋대로 끄게 된다. 그래서 파일 안에 박힌
// 이름으로 가린다. 둘 다 있을 때만 REFramework 으로 본다.
//
// 2026-10-23 바이오하자드 RE4 에서 확인. REFramework 과 우리 것을 같이 넣으면
// 게임을 시작하는 순간 꺼진다. 터진 모듈이 dinput8.dll 로 찍힌다.
// 제작자 깃허브에도 같은 제보가 여러 건 올라와 있고(#1635 #1590 #1683),
// 제작자 공지에 DLSS 프레임 생성이 무작위 크래시의 원인이라고 적혀 있다.
static bool IsREFramework(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) { return false; }
    std::vector<char> buf(1 << 20);
    std::string tail;
    DWORD got = 0;
    bool name = false, who = false;
    while (!(name && who) && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail + std::string(&buf[0], got);
        if (chunk.find("REFramework") != std::string::npos) { name = true; }
        if (chunk.find("praydog") != std::string::npos) { who = true; }
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
    return name && who;
}

// 설치 방식 판단용. exe 가 크면 예전과 똑같이 exe 만 본다.
// exe 가 작으면 옆의 게임 본체 dll 에 DLSS 가 들어 있는지도 본다.
static int GameSupportsDlssFor(const std::wstring& exe) {
    // 폴더에 엔비디아 파일이 있으면 그걸로 끝이다. exe 안 글자보다 확실하다.
    // 껍데기 exe 를 쓰는 엔진(캡콤 RE 엔진은 _storage_ 안에 본체가 있다)이나
    // 데누보로 암호화된 exe 는 글자를 뒤져도 안 나온다. (프래그마타 사례)
    size_t cut = exe.find_last_of(L"\\");
    if (cut != std::wstring::npos) {
        std::wstring dir = exe.substr(0, cut);

        // 스트림라인 파일은 우리가 절대 넣지 않는다. 있으면 게임이 받은 것이다.
        //
        // 2026-10-18. 다만 그것만으로는 모자란다. 데이즈곤이 2026-08-25
        // 업데이트에서 sl.*.dll 을 같이 받았는데, 게임 설정에는 DLSS 항목이 없다.
        // 파일만 굴러다니고 통로는 없는 상태다. 그걸 DLSS 게임으로 보고
        // 옵티 방식으로 깔면 리세이드로 잘 쓰시던 분이 쓰던 길을 잃는다.
        //
        // 그래서 실행 파일이 DLSS 를 아는지 같이 본다.
        //   DaysGone.exe 안에 NVSDK_NGX / nvngx / DLSS 글자가 하나도 없다.
        //   위쳐3·라오어 같은 진짜 DLSS 게임은 그 글자가 들어 있다.
        // 실행 파일이 모른다고 하면 폴더 파일은 안 믿는다.
        const wchar_t* sure[] = { L"sl.interposer.dll", L"sl.dlss.dll" };
        for (size_t i = 0; i < _countof(sure); i++) {
            if (!FileExists(dir + L"\\" + sure[i])) { continue; }
            if (GameSupportsDlss(exe) == 0) {
                Log(std::wstring(L"  ") + sure[i] + L" 은 있지만 실행 파일은 DLSS 를 모릅니다.");
                Log(L"  파일만 들어 있고 게임에는 DLSS 항목이 없는 경우입니다. 리세이드 방식으로 갑니다.");
                return 0;
            }
            Log(std::wstring(L"  ") + sure[i] + L" 이(가) 있습니다. DLSS 를 쓰는 게임입니다.");
            return 1;
        }
        // nvngx_dlss.dll 은 우리도 넣는 파일이라 게임 것인지 우리 것인지 가려야 한다.
        // 애드온이 있나로 가리던 줄이었는데, 되돌리기가 애드온만 지우고 이 파일은
        // 남기는 경우가 있어서 못 가렸다. (2026-10-23 바이오하자드 RE4)
        // 이제 파일 자체를 우리 것과 대조한다. 게임사는 자기가 넣은 판을 쓰므로
        // 크기와 CRC 가 우리 것과 똑같으면 우리가 넣은 것이다.
        if (FileExists(dir + L"\\" + DLSS_DLL)) {
            if (IsOurDlssRuntime(dir + L"\\" + DLSS_DLL)) {
                Log(std::wstring(L"  ") + DLSS_DLL + L" 은 우리가 넣었던 파일입니다. 증거로 쓰지 않습니다.");
            } else {
                Log(std::wstring(L"  ") + DLSS_DLL + L" 이(가) 있습니다. DLSS 를 쓰는 게임입니다.");
                return 1;
            }
        }
    }
    // 어느 파일에서 DLSS 를 찾았는지 기억해 둔다.
    // 2026-10-23 킹덤컴2. KingdomCome.exe 는 1.5MB 껍데기라 표식이 하나도 없고,
    // 옆의 WHGameArm.dll(102MB)에 NVSDK_NGX·nvngx 가 들어 있다.
    // 증거를 본체 dll 에서 찾아 놓고 아래 확실한 표식 검사를 껍데기 exe 로 하면,
    // 진짜 DLSS 게임이 리세이드 길로 내려간다.
    std::wstring probe = exe;
    int r = GameSupportsDlss(exe);
    if (r == 0 && FileSizeOf(exe) < GAME_EXE_MIN) {
        std::wstring eng = EngineDllNear(exe);
        if (!eng.empty()) {
            r = GameSupportsDlss(eng);
            if (r == 1) { probe = eng; }
        }
    }
    if (r != 1) { return r; }

    // 여기까지 왔다는 건, 실행 파일은 DLSS 를 아는데 그 옆에 DLSS 런타임이
    // 하나도 없다는 뜻이다. 위 검사에서 찾았으면 벌써 1 을 돌려줬을 것이다.
    //
    // 2026-10-23 바이오하자드 RE4. 캡콤 RE 엔진은 엔진 차원에서 DLSS 통로를 들고 있어
    // (via.render.DLSSUpscalingInterface, isDLSSAvailable) 어느 게임이든 글자가 잡힌다.
    // 같은 엔진이라도 쓰는 게임이 있고 안 쓰는 게임이 있다. RE4 는 안 쓴다 —
    // 엔비디아 앱에서도 DLSS 오버라이드 세 항목이 모두 "지원되지 않음" 으로 나온다.
    //
    // 쓰는 게임은 런타임을 들고 있다.
    //   위쳐3    bin/x64_dx12/nvngx_dlss.dll    58,956,912
    //   킹덤컴2   Bin/Win64Shared/nvngx_dlss.dll  48,971,808
    //   붉은사막  bin64/nvngx_dlss.dll           58,956,912
    //   RE4      없음
    //
    // OptiScaler 는 게임이 부르는 DLSS/FSR 을 가로채는 방식이다. 게임이 부르지 않으면
    // 올라와도 할 일이 없어 창조차 열리지 않는다. 그런 게임은 리세이드 길로 보낸다.
    // 확실한 표식(NVSDK_NGX · nvngx · sl.interposer)이 있으면 진짜 DLSS 게임이다.
    // 런타임 파일이 안 보이는 건 예전에 우리 파일이 그 자리에 들어가 앉았거나
    // 실행 파일 옆이 아닌 다른 폴더에 있는 경우다. 내려보내지 않는다.
    // 2026-10-23. 섀도우 오브 더 툼레이더가 여기서 뒤집힐 뻔했다.
    if (ExeHasStrongDlssMark(probe)) {
        Log(L"  DLSS 런타임이 안 보이지만 실행 파일에 엔비디아 호출이 들어 있습니다.");
        Log(L"  진짜 DLSS 게임이라 OptiScaler 방식으로 갑니다.");
        return 1;
    }

    // 불칸은 예외다. 리세이드 길에서 안 되는 것이 확인돼 있어서, 내려보내면
    // 갈 길이 아예 없어진다. 런타임이 없어도 OptiScaler 로 둔다.
    if (LooksLikeVulkanGame(probe)) {
        Log(L"  DLSS 런타임이 없지만 불칸 게임입니다.");
        Log(L"  불칸은 리세이드 방식으로 신경망을 걸 수 없어 OptiScaler 방식으로 갑니다.");
        return 1;
    }

    Log(L"  실행 파일은 DLSS 를 아는데 DLSS 런타임이 하나도 없습니다.");
    Log(L"  엔진이 통로만 들고 있고 이 게임은 쓰지 않는 경우입니다. 리세이드 방식으로 갑니다.");
    return 0;
}

// 파일 안에 적힌 그래픽 dll 이름을 찾는다.
// 유니코드로 적힌 이름도 잡으려고 0 바이트를 빼고 소문자로 바꿔서 찾는다.
static void ScanApiNames(const std::wstring& path, bool& d3d9, bool& modern) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    const char* mods[] = { "dxgi.dll", "d3d11.dll", "d3d12.dll", "vulkan-1.dll", "opengl32.dll" };
    std::vector<char> buf(1 << 20);
    std::string tail; DWORD got = 0;
    while (!modern && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail;
        chunk.reserve(tail.size() + got);
        for (DWORD i = 0; i < got; i++) {
            char c = buf[i];
            if (c == 0) continue;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            chunk.push_back(c);
        }
        if (!d3d9 && chunk.find("d3d9.dll") != std::string::npos) d3d9 = true;
        for (int i = 0; i < 5 && !modern; i++)
            if (chunk.find(mods[i]) != std::string::npos) modern = true;
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
}

// DirectX 9 전용 게임인가. (GTA4 등)
// DLSS5-Feeder 는 D3D11 / D3D12 / Vulkan / OpenGL 만 받는다. DX9 게임에는 넣어도 켜지지 않는다.
// d3d9.dll 이름은 있는데 dxgi / d3d11 / d3d12 / vulkan / opengl 이름이 하나도 없으면 DX9 전용으로 본다.
static bool IsDx9Only(const std::wstring& exe) {
    bool d3d9 = false, modern = false;
    ScanApiNames(exe, d3d9, modern);
    if (!modern && FileSizeOf(exe) < GAME_EXE_MIN) {
        std::wstring eng = EngineDllNear(exe);
        if (!eng.empty()) ScanApiNames(eng, d3d9, modern);
    }
    return d3d9 && !modern;
}
// 이 게임에서 전에 dxgi.dll 자리가 실패한 적이 있는가.
// OptiScaler 가 남긴 기록에 그 흔적이 있으면 두 번 시도하지 않는다.
static bool DxgiFailedBefore(const std::wstring& gameDir) {
    std::string t = ReadFileBytes(gameDir + L"\\OptiScaler.log");
    if (t.empty()) return false;
    std::string l = LowerA(t);
    return l.find("already in memory") != std::string::npos
        || l.find("failed to hook dlss") != std::string::npos
        || l.find("late streamline hook") != std::string::npos;
}

// 자리 순서에 이름을 넣는다. 이미 있으면 안 넣는다.
//
// 2026-10-24. 전에는 부르는 자리마다 for 문으로 중복을 확인했는데, 블록 끝에서
// version.dll 과 dbghelp.dll 을 한 번 더 넣는 줄이 있어 세 갈래 모두 두 번씩
// 들어갔다. order 가 [winmm, version, dbghelp, version, dbghelp] 가 된다.
// 고르는 결과는 같지만 기록이 지저분하고, 다음에 손댈 때 또 헷갈린다.
static void AddSlot(std::vector<std::wstring>& order, const wchar_t* name) {
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == name) { return; }
    }
    order.push_back(name);
}

static std::wstring ChooseProxyName(const std::wstring& gameDir, bool& vulkanGuess) {
    // 이미 OptiScaler 가 들어가 있으면 무조건 그 자리를 쓴다.
    // 이걸 먼저 안 보면 한 게임에 두 벌이 올라가 게임이 바로 꺼진다.
    // 2026-10-09 위처3: dxgi.dll 에 이미 있는데 스트림라인 분기가 winmm.dll 을
    // 먼저 고르는 바람에 두 벌이 됐다. 아래 순서 고르기보다 항상 앞에 와야 한다.
    {
        const wchar_t* all[] = { L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll",
                                 L"wininet.dll", L"winhttp.dll", L"d3d11.dll", L"d3d12.dll" };
        for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++) {
            std::wstring p = gameDir + L"\\" + all[i];
            if (!FileExists(p) || !IsOptiScalerFile(p)) continue;

            // 다만 10-09 ~ 10-17 판이 스트림라인 게임을 winmm.dll 자리에
            // 넣어 놨다면, 그 자리에서는 Insert 창이 영영 안 열린다.
            // 그대로 두면 [설치]를 다시 눌러도 같은 자리에 머물러서
            // 아무것도 안 고쳐진다. 그래서 그 경우만 치우고 dxgi.dll 로 옮긴다.
            // dxgi.dll 이 비어 있을 때만 한다. 남이 쓰고 있으면 지금 자리가 최선이다.
            // 2026-10-18: 옮겨도 되는 게임인지 먼저 본다. 호라이즌처럼
            // dxgi.dll 을 직접 안 부르는 게임을 옮기면 게임이 꺼진다.
            // ── 1.0.1.3 이 넣은 dxgi.dll 을 winmm.dll 로 되돌린다.
            //
            // 2026-10-18. 1.0.1.3 은 스트림라인 게임을 dxgi.dll 자리에 넣었고,
            // 호라이즌처럼 늦게 올라오는 게임은 켜자마자 꺼졌다.
            // 그런데 이 함수는 "이미 OptiScaler 가 있으면 그 자리를 쓴다"가 먼저라,
            // 받으신 분이 [설치]를 다시 눌러도 dxgi 에 그대로 머물러 안 고쳐진다.
            // 그래서 스트림라인 게임이 dxgi 자리에 있으면 치우고 winmm 으로 보낸다.
            // [되돌리기]를 먼저 누르라고 안내할 수도 있지만, 그 줄을 지나치는
            // 분이 반드시 나온다. 프로그램이 해야 한다.
            //
            // 2026-10-24 (1.0.3.2). 게임 이름 목록을 없앴다.
            // 스트림라인 게임은 전부 dxgi 자리로 간다. 이름으로 가르면 목록에
            // 없는 게임이 계속 나온다 (스파이더맨 2 제보). 그 PC 에서 실제로
            // 꺼진 흔적이 있을 때만 winmm 으로 되돌린다.
            //
            // 2026-10-18 추가. "dxgi 자리에 있다"만 보면 안 된다.
            // 붉은사막처럼 그 자리에서 잘 돌고 있는 게임까지 winmm 으로 돌려보내고,
            // 그러면 다음 설치 때 또 dxgi 로 옮겨서 왔다 갔다 한다.
            // 그 PC 에서 실제로 꺼진 흔적이 있을 때만 되돌린다.
            if (LowerW(all[i]) == L"dxgi.dll"
                && FileExists(gameDir + L"\\sl.interposer.dll")
                && !FileExists(gameDir + L"\\winmm.dll")
                && DxgiFailedBefore(gameDir)) {
                Log(L"  dxgi.dll 자리에 들어 있습니다. 스트림라인 게임에서는 게임이 꺼질 수 있는 자리입니다.");
                Log(L"  winmm.dll 자리로 되돌립니다.");
                if (DeleteFileW(p.c_str())) {
                    Log(L"  옛 dxgi.dll 을 치웠습니다.");
                    // 2026-10-24. 백업본이 우리 것이면 돌려놓지 않는다.
                    // 옛 판이 우리 파일을 "원래 파일"로 알고 백업해 둔 경우가 있어서,
                    // 그대로 돌려놓으면 두 자리에 OptiScaler 가 하나씩 생긴다.
                    std::wstring bak = gameDir + L"\\" + BACKUP_DIR + L"\\dxgi.dll";
                    if (FileExists(bak) && IsOurBackupCandidate(bak, L"dxgi.dll")) {
                        Log(L"  백업본이 우리가 넣은 파일입니다. 돌려놓지 않고 그 백업본도 치웁니다.");
                        DeleteFileW(bak.c_str());
                    } else if (FileExists(bak) && CopyFileW(bak.c_str(), p.c_str(), FALSE)) {
                        Log(L"  게임에 원래 있던 dxgi.dll 을 제자리에 돌려놨습니다.");
                    }
                    continue;   // 아래 순서 고르기로 내려가 winmm.dll 을 고른다
                }
                Log(L"  [경고] 옛 dxgi.dll 을 치우지 못했습니다. 게임을 끄고 다시 눌러 주세요.");
            }

            // winmm 자리에서 dxgi 자리로 옮기는 경우.
            // 1.0.1.2 이하로 깔아 두신 분이 [설치]만 눌러도 옮겨져야 한다.
            //
            // 2026-10-24 (1.0.3.2). 조건을 넓혔다.
            // 전에는 게임 이름 목록(위쳐3·라오어1)에 있거나, 그 PC 기록에
            // winmm 에서 막힌 흔적이 남아 있어야만 옮겼다. 스파이더맨 2 는
            // 목록에 없고, 1.0.2.0 기록에는 막힌 흔적도 안 남는다. 그래서
            // 두 분(@께·@J_s_iny)이 [설치]를 다시 눌러도 winmm 에 머물렀고
            // OptiScaler.ini 를 손으로 고쳐야 했다.
            // 이제 스트림라인 게임이 winmm 에 있고 dxgi 자리가 비어 있으면
            // 무조건 옮긴다. 그 PC 에 dxgi 실패 기록이 있을 때만 안 옮긴다.
            bool stuck = (LowerW(all[i]) == L"winmm.dll")
                      && FileExists(gameDir + L"\\sl.interposer.dll")
                      && !FileExists(gameDir + L"\\dxgi.dll")
                      && !DxgiFailedBefore(gameDir);
            if (stuck) {
                Log(L"  winmm.dll 자리에 들어 있습니다. 이 자리에서는 Insert 창이 안 열립니다.");
                Log(L"  dxgi.dll 자리가 비어 있어 그쪽으로 옮깁니다.");
                if (DeleteFileW(p.c_str())) {
                    Log(L"  옛 winmm.dll 을 치웠습니다.");
                    // 2026-10-24. 백업본이 우리 것이면 돌려놓지 않는다.
                    // @J_s_iny 님 스파이더맨 2 가 바로 이 경우다. 백업 폴더의
                    // winmm.dll 이 OptiScaler 라서, 돌려놓으면 winmm 과 dxgi 에
                    // 하나씩 생긴다.
                    std::wstring bak = gameDir + L"\\" + BACKUP_DIR + L"\\winmm.dll";
                    if (FileExists(bak) && IsOurBackupCandidate(bak, L"winmm.dll")) {
                        Log(L"  백업본이 우리가 넣은 파일입니다. 돌려놓지 않고 그 백업본도 치웁니다.");
                        DeleteFileW(bak.c_str());
                    } else if (FileExists(bak) && CopyFileW(bak.c_str(), p.c_str(), FALSE)) {
                        Log(L"  게임에 원래 있던 winmm.dll 을 제자리에 돌려놨습니다.");
                    }
                    continue;   // 아래 순서 고르기로 내려가 dxgi.dll 을 고른다
                }
                Log(L"  [경고] 옛 winmm.dll 을 치우지 못했습니다. 게임을 끄고 다시 눌러 주세요.");
            }

            Log(std::wstring(L"  이미 ") + all[i] + L" 에 OptiScaler 가 들어 있습니다. "
                L"새 자리를 만들지 않고 그 파일을 갱신합니다.");
            return all[i];
        }
    }

    std::wstring l = LowerW(gameDir);
    // 창에서 고르신 값이 우선이다. 0 = 자동, 1 = DirectX, 2 = Vulkan
    LRESULT sel = 0;
    if (sel == 2) {
        vulkanGuess = true;
        Log(L"  그래픽 방식: Vulkan (직접 고르셨습니다)");
    } else if (sel == 1) {
        vulkanGuess = false;
        Log(L"  그래픽 방식: DirectX (직접 고르셨습니다)");
    } else {
        vulkanGuess = (l.find(L"red dead redemption 2") != std::wstring::npos
                    || l.find(L"doom") != std::wstring::npos
                    || l.find(L"wolfenstein") != std::wstring::npos
                    || l.find(L"no man's sky") != std::wstring::npos);
        Log(vulkanGuess ? L"  그래픽 방식: Vulkan (게임 이름으로 판단했습니다)"
                        : L"  그래픽 방식: DirectX (게임 이름으로 판단했습니다)");
        Log(L"  안 되면 [진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요.");
    }
    std::vector<std::wstring> order;
    if (vulkanGuess) { AddSlot(order, L"winmm.dll"); }

    // GTA5 인핸스드는 dxgi.dll 을 아예 안 불러온다.
    // GTA5_Enhanced.exe 의 가져오기 목록에 dxgi 가 없어서, 그 이름으로 넣으면
    // 게임은 켜지는데 창이 안 뜬다. 오피스케일러 위키도 이 게임은
    // dxgi 와 winmm 둘 다 적어 두었는데, 안 된다는 제보는 전부 dxgi 쪽이다.
    // 2026-10-11 제보. 게임이 없어서 폴더로는 확인하지 못했다.
    if (l.find(L"grand theft auto") != std::wstring::npos || l.find(L"gtav") != std::wstring::npos) {
        Log(L"  GTA5 입니다. dxgi.dll 을 안 불러오는 게임이라 winmm.dll 로 넣습니다.");
        Log(L"  이 게임은 런처의 BattlEye 를 끄지 않으면 설치해도 창이 안 뜹니다.");
        AddSlot(order, L"winmm.dll");
        AddSlot(order, L"version.dll");
    }

    // 엔비디아 스트림라인을 쓰는 게임. 여기서 두 번 데었으니 기록을 길게 남긴다.
    //
    // 2026-09-19 (1.0.0.5) 데스스트랜딩·NBA 2K27 제보.
    //   dxgi.dll 자리에 넣었더니 slInit() 이 0x18 로 실패하고 화면에 뜬다 —
    //     Late Streamline hook detected
    //     Consider renaming OptiScaler from dxgi.dll to other supported name
    //   그래서 스트림라인 게임은 무조건 winmm.dll 로 보내게 했다.
    //
    // 2026-10-17 라오어 1. winmm.dll 자리에서는 Insert 창이 안 열린다.
    //   스팀 오버레이가 DXGI 를 먼저 가로채서 OptiScaler 가 전부 건너뛴다 —
    //     hkCreateDXGIFactory2 Skipping wrapping for: gameoverlayrenderer64.dll
    //   dxgi.dll 로 옮기니 바로 열렸다. 그래서 전부 dxgi 로 되돌렸다. 틀렸다.
    //
    // 2026-10-18 호라이즌. 그 판으로 설치했더니 게임이 켜자마자 꺼졌다 —
    //     CheckWorkingMode sl.common.dll already in memory
    //     StreamlineHooks::hookDlss Failed to hook DLSS: 10DD
    //
    // 그때는 exe 의 가져오기 표로 가르면 두 사고가 설명된다고 봤다.
    //   라오어   dxgi.dll 을 표에 적어 둔다  → 게임과 동시에 올라온다 → 안 늦다
    //   호라이즌 안 적어 둔다               → 나중에 올라온다 → 스트림라인이 먼저다
    // 그래서 적어 둔 게임에만 dxgi 를 줬다.
    //
    // 2026-10-24. 그 기준을 버렸다. 옵티 포크 v0.1.33 에서는 호라이즌이
    // 표에 안 적어 둔 채로도 dxgi 에서 멀쩡히 돌아간다. 아래 블록 참고.
    if (FileExists(gameDir + L"\\sl.interposer.dll")) {
        bool failedBefore = DxgiFailedBefore(gameDir);

        // ── dxgi.dll 자리에서 꺼지는 것이 확인된 게임.
        //
        // 프래그마타 (2026-10-18, 여러 번 확인)
        //   순정                     잘 켜진다
        //   dxgi 자리에 설치          첫 실행만 켜지고 두 번째부터 안 켜진다
        //   리세이드를 빼고 설치해도   똑같다. 리세이드 탓이 아니다
        //   9월(1.0.0.5)에는 winmm 자리에서 정상 확인된 게임이다
        //
        // exe 가 dxgi.dll 을 직접 부르니 안전할 것이라고 봤는데 이 게임에서 틀렸다.
        // 켜 보기 전에는 알 수 없으므로, 터진 게임은 이름으로 적어 둔다.
        bool knownBad = (l.find(L"pragmata") != std::wstring::npos);

        // ── 2026-10-24 (1.0.3.2). 스트림라인 게임은 dxgi 로 보낸다.
        //
        // 10월 18일에는 호라이즌이 dxgi 자리에서 게임째로 꺼져서, 직접 켜서
        // 확인한 게임(위쳐3·라오어1)만 dxgi 를 주고 나머지는 winmm 으로 보냈다.
        // 그 뒤로 옵티 포크가 바뀌었고, 10월 24일에 같은 게임을 dxgi 로 다시
        // 켜 보니 멀쩡히 돌아간다.
        //   호라이즌 dxgi (v0.1.33)  게임 켜짐 · 창 뜸
        //                            DLSSFeatureDx12::InitDLSS _CreateFeature result: Success
        //                            10월에 죽이던 Failed to hook DLSS: 10DD 가 없다
        //
        // winmm 자리는 누가 DXGI 팩토리를 먼저 만드느냐에 따라 창이 안 뜬다.
        //   스파이더맨 2  우리 리세이드(d3d12)가 먼저 만들어서 옵티가 비켜선다
        //                hkCreateDXGIFactory1 Skipping wrapping for: d3d12.dll
        //                게임은 켜지고 옵티도 올라오는데 창만 안 뜬다. 제보 2건
        //   호라이즌      게임이 먼저 만들어서 비켜설 일이 없다
        // 게임마다 갈려서 미리 알 수가 없다. dxgi 자리면 옵티가 DXGI 그 자체라
        // 비켜설 일이 아예 없다.
        //
        // 게임 이름을 하나씩 적는 목록은 제보가 올 때마다 늘어난다. 그 방식을 버렸다.
        // 전에 쓰던 exe 가져오기 표(ExeImportsDll)도 안 본다. 라오어1 은 부르고
        // 호라이즌은 안 부르는데 둘 다 dxgi 에서 돌아간다. 가르는 기준이 아니었다.
        // 단, 그 PC 의 기록에 dxgi 실패 흔적이 있으면 winmm 으로 간다.
        if (knownBad) {
            Log(L"  엔비디아 스트림라인을 쓰는 게임입니다.");
            Log(L"  이 게임은 dxgi.dll 자리에서 꺼지는 것이 확인됐습니다. winmm.dll 로 넣습니다.");
            AddSlot(order, L"winmm.dll");
        } else if (!failedBefore) {
            Log(L"  엔비디아 스트림라인을 쓰는 게임입니다. dxgi.dll 로 넣습니다.");
            AddSlot(order, L"dxgi.dll");
            AddSlot(order, L"winmm.dll");
        } else {
            Log(L"  엔비디아 스트림라인을 쓰는 게임입니다.");
            Log(L"  이 PC 에서 dxgi.dll 자리가 막힌 기록이 있어 winmm.dll 로 넣습니다.");
            AddSlot(order, L"winmm.dll");
        }
        AddSlot(order, L"version.dll");
        AddSlot(order, L"dbghelp.dll");
    }
    for (int i = 0; i < (int)(sizeof(PROXY_NAMES) / sizeof(PROXY_NAMES[0])); i++) {
        bool dup = false;
        for (size_t j = 0; j < order.size(); j++) if (order[j] == PROXY_NAMES[i]) dup = true;
        if (!dup) order.push_back(PROXY_NAMES[i]);
    }
    for (size_t i = 0; i < order.size(); i++) {
        std::wstring full = gameDir + L"\\" + order[i];
        if (!FileExists(full)) return order[i];
        if (IsOptiScalerFile(full)) {
            Log(L"  " + order[i] + L" 은(는) 이미 OptiScaler입니다. 새로 만들지 않고 이 파일을 갱신합니다.");
            return order[i];
        }
        Log(L"  " + order[i] + L" 은(는) 다른 프로그램이 쓰고 있어 건너뜁니다.");
    }
    return order[0];
}


// ================================================================ ini 설정값 넣기
static void ApplyPreset(const std::wstring& iniPath) {
    std::string text = ReadFileBytes(iniPath);
    if (text.empty()) { Log(L"[경고] OptiScaler.ini 를 찾지 못해 설정값을 넣지 못했습니다."); return; }
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
        lines.push_back(text.substr(start, nl - start + 1));
        start = nl + 1;
    }
    std::string section;
    int changed = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = lines[i];
        size_t a = t.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        if (t[a] == '[') {
            size_t b = t.find(']', a);
            if (b != std::string::npos) section = t.substr(a + 1, b - a - 1);
            continue;
        }
        if (t[a] == ';' || t[a] == '#') continue;
        size_t eq = t.find('=', a);
        if (eq == std::string::npos) continue;
        std::string key = t.substr(a, eq - a);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        for (size_t k = 0; k < sizeof(PRESET) / sizeof(PRESET[0]); k++) {
            if (section == W2U(PRESET[k].section) && key == W2U(PRESET[k].key)) {
                lines[i] = key + " = " + W2U(PRESET[k].value) + "\r\n";
                changed++;
                break;
            }
        }
    }
    // 2026-10-22. ini 틀에 그 줄이 없는 항목은 위에서 고쳐지지 않는다.
    // 전에는 그런 항목이 조용히 버려졌다. 로그에도 안 남아서 모르고 지나간다.
    // 실제로 janblade 포크의 틀에는 Transfer 줄이 없어서
    // Natural 의 일부인 Transfer = 2 가 안 써졌다.
    // 없는 항목은 그 단락 끝에 붙인다. 단락자체가 없으면 맨 뒤에 만든다.
    int addedCnt = 0;
    for (size_t k = 0; k < sizeof(PRESET) / sizeof(PRESET[0]); k++) {
        std::string wantSec = W2U(PRESET[k].section);
        std::string wantKey = W2U(PRESET[k].key);
        bool have = false;
        std::string sec2;
        size_t secEnd = (size_t)-1;   // 그 단락의 마지막 줄 다음 자리
        for (size_t i = 0; i < lines.size() && !have; i++) {
            std::string t = lines[i];
            size_t a = t.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            if (t[a] == '[') {
                size_t b2 = t.find(']', a);
                if (b2 != std::string::npos) {
                    if (sec2 == wantSec && secEnd == (size_t)-1) secEnd = i;
                    sec2 = t.substr(a + 1, b2 - a - 1);
                }
                continue;
            }
            if (t[a] == ';' || t[a] == '#') continue;
            size_t eq = t.find('=', a);
            if (eq == std::string::npos) continue;
            std::string key = t.substr(a, eq - a);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            if (sec2 == wantSec && key == wantKey) have = true;
        }
        if (have) continue;
        std::string line = wantKey + " = " + W2U(PRESET[k].value) + "\r\n";
        if (secEnd != (size_t)-1) {
            lines.insert(lines.begin() + secEnd, line);
        } else {
            bool secFound = false;
            for (size_t i = 0; i < lines.size(); i++) {
                std::string t = lines[i];
                size_t a = t.find_first_not_of(" \t");
                if (a == std::string::npos || t[a] != '[') continue;
                size_t b2 = t.find(']', a);
                if (b2 != std::string::npos && t.substr(a + 1, b2 - a - 1) == wantSec) { secFound = true; break; }
            }
            if (!secFound) lines.push_back("[" + wantSec + "]\r\n");
            lines.push_back(line);
        }
        addedCnt++;
        Log(L"  없던 항목을 붙였습니다: " + std::wstring(PRESET[k].key));
    }

    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += lines[i];
    WriteFileBytes(iniPath, out);
    wchar_t b[160];
    swprintf_s(b, L"  설정 %d개를 적용했습니다. (새로 붙인 것 %d개)", changed, addedCnt);
    Log(b);
}


// 게임 폴더에 리세이드가 이미 깔려 있는가.
// 리세이드가 dxgi.dll 등으로 먼저 들어가 DXGI 팩토리를 감싸면
// OptiScaler 가 그 위에 훅을 걸지 못한다. A 방식 설치에서 이걸 보고 대응한다.
static bool HasReShade(const std::wstring& gameDir) {
    const wchar_t* hooks[] = { L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"d3d9.dll", L"opengl32.dll" };
    for (int i = 0; i < 5; i++) {
        std::wstring p = gameDir + L"\\" + hooks[i];
        if (FileExists(p) && IsReShadeFile(p)) return true;
    }
    return false;
}

// 파일 크기 (없으면 -1)
static long long FileSizeOf(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return -1;
    LARGE_INTEGER li; li.HighPart = fad.nFileSizeHigh; li.LowPart = fad.nFileSizeLow;
    return li.QuadPart;
}

// 파일 CRC32 — 어느 변종인지 식별하는 유일한 수단이다 (크기가 같아도 내용이 다를 수 있다)
static std::wstring Crc32File(const std::wstring& p) {
    static unsigned long tbl[256]; static bool init = false;
    if (!init) {
        for (unsigned long i = 0; i < 256; i++) {
            unsigned long c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return L"(읽기 실패)";
    static std::vector<unsigned char> buf(1 << 20);
    unsigned long crc = 0xFFFFFFFFUL; DWORD got = 0;
    while (ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0)
        for (DWORD i = 0; i < got; i++) crc = tbl[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    CloseHandle(h);
    crc ^= 0xFFFFFFFFUL;
    wchar_t b[16]; swprintf_s(b, L"%08lX", crc);
    return b;
}

// 파일 크기와 CRC 를 한 줄로
static std::wstring FileFingerprint(const std::wstring& p) {
    long long sz = FileSizeOf(p);
    if (sz < 0) return L"(없음)";
    wchar_t b[64]; swprintf_s(b, L"%lld 바이트  CRC ", sz);
    return std::wstring(b) + Crc32File(p);
}

// 드라이브 여유 공간 (MB)
static long long FreeSpaceMB(const std::wstring& dir) {
    ULARGE_INTEGER freeAvail = {}, total = {}, freeTotal = {};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &freeAvail, &total, &freeTotal)) return -1;
    return (long long)(freeAvail.QuadPart / (1024 * 1024));
}

// 관리자 권한으로 실행 중인가
static bool IsElevated() {
    HANDLE tok = NULL; TOKEN_ELEVATION el = {}; DWORD sz = sizeof(el);
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz);
    CloseHandle(tok);
    return ok && el.TokenIsElevated != 0;
}

// ini 항목 하나만 바꾼다. 항목이 없으면 그 섹션 끝에 만들고, 섹션도 없으면 섹션째 만든다.
static bool SetIniKey(const std::wstring& iniPath, const char* wantSec,
                      const char* wantKey, const char* val) {
    const char NL = (char)10;
    static const char WSCH[] = { 32, (char)9, 0 };
    std::string text = ReadFileBytes(iniPath);
    if (text.empty()) return false;
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(NL, start);
        if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
        lines.push_back(text.substr(start, nl - start + 1));
        start = nl + 1;
    }
    std::string section; bool hit = false;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = lines[i];
        size_t a = t.find_first_not_of(WSCH);
        if (a == std::string::npos) continue;
        if (t[a] == '[') {
            size_t b = t.find(']', a);
            if (b != std::string::npos) section = t.substr(a + 1, b - a - 1);
            continue;
        }
        if (t[a] == ';' || t[a] == '#') continue;
        size_t eq = t.find('=', a);
        if (eq == std::string::npos) continue;
        std::string key = t.substr(a, eq - a);
        while (!key.empty() && (key.back() == 32 || key.back() == (char)9)) key.pop_back();
        if (section == wantSec && key == wantKey) {
            std::string crlf; crlf += (char)13; crlf += (char)10;
            lines[i] = key + " = " + val + crlf;
            hit = true; break;
        }
    }
    // 키가 아예 없으면 그 섹션 끝에 새로 넣는다.
    // OptiScaler 는 ini 에 없어도 코드에서 읽는 키가 있어서, 값을 강제하려면 줄을 만들어야 한다.
    if (!hit) {
        std::string want = std::string("[") + wantSec + "]";
        int secLine = -1, insertAt = -1;
        for (size_t i = 0; i < lines.size(); i++) {
            std::string t = lines[i];
            size_t a = t.find_first_not_of(WSCH);
            if (a == std::string::npos) continue;
            if (t[a] != '[') continue;
            if (t.compare(a, want.size(), want) == 0) { secLine = (int)i; continue; }
            if (secLine >= 0) { insertAt = (int)i; break; }
        }
        if (secLine < 0) {
            // 섹션 자체가 없으면 파일 끝에 섹션과 키를 함께 만든다.
            std::string crlf; crlf += (char)13; crlf += (char)10;
            if (!lines.empty() && !lines.back().empty()) lines.push_back(crlf);
            lines.push_back(std::string("[") + wantSec + "]" + crlf);
            lines.push_back(std::string(wantKey) + " = " + val + crlf);
            std::string out2;
            for (size_t i = 0; i < lines.size(); i++) out2 += lines[i];
            return WriteFileBytes(iniPath, out2);
        }
        if (insertAt < 0) insertAt = (int)lines.size();
        std::string crlf; crlf += (char)13; crlf += (char)10;
        lines.insert(lines.begin() + insertAt, std::string(wantKey) + " = " + val + crlf);
        hit = true;
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += lines[i];
    return WriteFileBytes(iniPath, out);
}


// ini 에서 값 하나를 읽는다. 없으면 빈 문자열.
static std::string GetIniKey(const std::wstring& iniPath, const char* wantSec,
                             const char* wantKey) {
    const char NL = (char)10;
    static const char WSCH[] = { 32, (char)9, (char)13, 0 };
    std::string text = ReadFileBytes(iniPath);
    if (text.empty()) return "";
    std::string want = std::string("[") + wantSec + "]";
    std::string section;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(NL, start);
        std::string t = (nl == std::string::npos) ? text.substr(start)
                                                  : text.substr(start, nl - start);
        start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        size_t a = t.find_first_not_of(WSCH);
        if (a == std::string::npos) continue;
        if (t[a] == ';') continue;
        if (t[a] == '[') {
            size_t b = t.find(']', a);
            if (b != std::string::npos) section = t.substr(a, b - a + 1);
            continue;
        }
        if (section != want) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = t.substr(a, eq - a);
        size_t ke = key.find_last_not_of(WSCH);
        if (ke == std::string::npos) continue;
        key.erase(ke + 1);
        if (LowerA(key) != LowerA(wantKey)) continue;
        std::string val = t.substr(eq + 1);
        size_t vs = val.find_first_not_of(WSCH);
        if (vs == std::string::npos) return "";
        size_t ve = val.find_last_not_of(WSCH);
        return val.substr(vs, ve - vs + 1);
    }
    return "";
}

// ini 는 바이트로 다루므로, 경로를 넣으려면 UTF-8 로 바꿔야 한다.
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) { return std::string(); }
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) { return std::string(); }
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

// 리세이드가 구운 셰이더를 어디에 둘지 정한다.
//
// 2026-10-23. 안 적어 주면 리세이드는 윈도우 임시 폴더를 쓴다.
// 그런데 모든 게임이 그 폴더 하나를 같이 쓴다. 이 PC 에서 8일 만에
// 파일 159,746개 2.3GB 가 쌓여 있었다. 두 가지가 터진다.
//   1. 저장 공간 센스나 디스크 정리가 임시 폴더를 비우면 886개를 처음부터 다시 굽는다
//   2. 파일 16만 개짜리 폴더에서 찾느라, 캐시를 쓰는 게 안 쓰는 것만큼 느려진다
// 게임 폴더에 두면 둘 다 없어진다.
//   바이오하자드 RE4 실측 (효과 1023개): 처음 굽기 27.6초 -> 그다음 3.5초.
//
// 사용자가 손수 정해 둔 자리는 건드리지 않는다. 임시 폴더를 가리킬 때만 바꾼다.
static void SetReShadeCachePath(const std::wstring& iniPath) {
    if (!FileExists(iniPath)) { return; }
    size_t cut0 = iniPath.find_last_of(L"\\");
    if (cut0 == std::wstring::npos) { return; }
    std::wstring want = iniPath.substr(0, cut0) + L"\\reshade-cache";

    std::string cur = GetIniKey(iniPath, "GENERAL", "IntermediateCachePath");
    std::string low = LowerA(cur);
    std::string wantLow = LowerA(WideToUtf8(want));
    // 이미 우리 자리를 가리키고 있어도 그냥 넘어가면 안 된다. 폴더가 없으면
    // 리세이드는 또 임시 폴더로 돌아간다. 2026-10-23 킹덤컴2 에서 걸렸다.
    // 전체 경로로만 견주면 안 된다. 중간 빌드가 상대 경로(.\reshade-cache)로 적어 둔
    // 값이 남아 있으면 못 알아보고 "직접 정하신 것" 으로 넘어간다.
    // 2026-10-23 데이즈곤에서 걸렸다. 끝이 reshade-cache 면 우리 자리로 본다.
    std::string tailName = "reshade-cache";
    bool endsWithOurs = low.size() >= tailName.size()
                     && low.compare(low.size() - tailName.size(), tailName.size(), tailName) == 0;
    bool alreadyOurs = (low == wantLow) || endsWithOurs;
    bool mine = cur.empty()
             || alreadyOurs
             || low.find("\\temp\\") != std::string::npos
             || low.find("/temp/")  != std::string::npos;
    if (!mine) {
        Log(L"  셰이더 캐시 자리는 직접 정해 두셔서 그대로 둡니다.");
        return;
    }
    // 전체 경로까지 똑같을 때만 그대로 둔다. 상대 경로로 적혀 있으면 아래에서 다시 적는다.
    if (low == wantLow) {
        if (!DirExists(want)) { CreateDirectoryW(want.c_str(), NULL); }
        if (DirExists(want)) {
            Log(L"  셰이더 캐시 자리는 이미 게임 폴더입니다. 폴더가 있는지만 확인했습니다.");
        } else {
            Log(L"  [경고] 셰이더 캐시 폴더를 만들지 못했습니다. 임시 폴더를 그대로 씁니다.");
        }
        return;
    }
    // 2026-10-23. 폴더를 먼저 만들어야 한다.
    // 리세이드는 없는 폴더를 가리키면 말없이 윈도우 임시 폴더로 돌아간다.
    // 라오어1 에서 설정은 들어갔는데 효과 994개가 전부 임시 폴더로 들어간 것을 보고 찾았다.
    // 자리도 전체 경로로 적는다. 상대 경로는 게임이 작업 폴더를 바꾸면 엉뚱한 데를 가리킨다.
    size_t cut = iniPath.find_last_of(L"\\");
    if (cut == std::wstring::npos) { return; }
    std::wstring dir = iniPath.substr(0, cut) + L"\\reshade-cache";
    if (!DirExists(dir)) { CreateDirectoryW(dir.c_str(), NULL); }
    if (!DirExists(dir)) {
        Log(L"  [경고] 셰이더 캐시 폴더를 만들지 못했습니다. 임시 폴더를 그대로 씁니다.");
        return;
    }
    std::string val = WideToUtf8(dir);
    if (SetIniKey(iniPath, "GENERAL", "IntermediateCachePath", val.c_str())) {
        Log(L"  셰이더 캐시를 게임 폴더에 두도록 했습니다. (두 번째 실행부터 빨라집니다)");
    }
}

// 다운로드 폴더에 받아 둔 압축 파일에서 신경망 파일만 꺼낸다
static bool ExtractFromArchives(std::wstring& out) {
    wchar_t prof[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) return false;
    std::wstring dl = std::wstring(prof) + L"\\Downloads";
    if (!DirExists(dl)) return false;

    const wchar_t* pats[] = { L"\\*.zip", L"\\*.7z", L"\\*.rar" };
    std::vector<std::wstring> cands;
    for (int e = 0; e < 3; e++) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dl + pats[e]).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring n = fd.cFileName, ln = LowerW(n);
            LARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
            if (sz.QuadPart < 40LL * 1024 * 1024) continue;
            if (ln.find(L"dlss") == std::wstring::npos && ln.find(L"renodx") == std::wstring::npos
                && ln.find(L"opti") == std::wstring::npos && ln.find(L"neural") == std::wstring::npos) continue;
            cands.push_back(dl + L"\\" + n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (cands.empty()) return false;

    std::wstring work = TempDir() + L"unpack";
    for (size_t i = 0; i < cands.size(); i++) {
        Log(L"     받아 두신 압축 파일을 열어 봅니다: " + cands[i]);
        Status(L"받아 둔 압축에서 신경망 파일을 꺼내는 중입니다...");
        RemoveTree(work); CreateDirectoryW(work.c_str(), NULL);
        std::wstring cmd = L"tar.exe -xf \"" + cands[i] + L"\" -C \"" + work + L"\"";
        if (!RunHidden(cmd, work)) {
            Log(L"     이 압축은 윈도우가 바로 열지 못합니다. 마우스 오른쪽 클릭 → 압축 풀기 후 다시 눌러 주세요.");
            continue;
        }
        if (FindFileDeep(work, NR_DLL, 5, out)) { Log(L"     압축 안에서 찾았습니다."); return true; }
        Log(L"     이 압축 안에는 없습니다.");
    }
    return false;
}

// 이 PC에서 압축을 풀 수 있는 프로그램들을 찾는다 (rar 은 tar.exe 로 안 된다)
static std::vector<std::wstring> FindExtractors() {
    std::vector<std::wstring> v;
    const wchar_t* cands[] = {
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe",
        L"C:\\Program Files\\Bandizip\\bz.exe",
        L"C:\\Program Files (x86)\\Bandizip\\bz.exe",
        L"C:\\Program Files\\WinRAR\\UnRAR.exe",
        L"C:\\Program Files (x86)\\WinRAR\\UnRAR.exe",
        L"C:\\Program Files\\WinRAR\\WinRAR.exe",
        L"C:\\Program Files (x86)\\WinRAR\\WinRAR.exe",
    };
    for (int i = 0; i < 8; i++) if (FileExists(cands[i])) v.push_back(cands[i]);
    std::wstring r = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\7-Zip", L"Path");
    if (!r.empty()) { std::wstring p = r + L"7z.exe"; if (FileExists(p)) v.push_back(p); }
    return v;
}

// 압축 하나에서 신경망 파일만 꺼낸다. zip 은 윈도우 기본 tar, rar 은 압축 프로그램이 필요하다.
static bool ExtractNrFromArchive(const std::wstring& archive, std::wstring& out) {
    std::wstring work = TempDir() + L"unpack";
    std::wstring la = LowerW(archive);
    bool isRar = (la.size() > 4 && la.compare(la.size() - 4, 4, L".rar") == 0);

    if (!isRar) {
        RemoveTree(work); CreateDirectoryW(work.c_str(), NULL);
        std::wstring cmd = L"tar.exe -xf \"" + archive + L"\" -C \"" + work + L"\"";
        if (RunHidden(cmd, work) && FindFileDeep(work, NR_DLL, 5, out)) return true;
    }

    std::vector<std::wstring> tools = FindExtractors();
    for (size_t i = 0; i < tools.size(); i++) {
        std::wstring t = tools[i], lt = LowerW(t), cmd;
        RemoveTree(work); CreateDirectoryW(work.c_str(), NULL);
        if (lt.find(L"7z.exe") != std::wstring::npos)
            cmd = L"\"" + t + L"\" x -y -o\"" + work + L"\" \"" + archive + L"\"";
        else if (lt.find(L"bz.exe") != std::wstring::npos)
            cmd = L"\"" + t + L"\" x -y -o:\"" + work + L"\" \"" + archive + L"\"";
        else
            cmd = L"\"" + t + L"\" x -y -ibck \"" + archive + L"\" \"" + work + L"\\\"";
        Log(L"     압축을 여는 중입니다: " + t);
        RunHidden(cmd, work);
        if (FindFileDeep(work, NR_DLL, 5, out)) return true;
    }

    if (isRar && tools.empty()) {
        Log(L"     [알림] 이 파일은 rar 형식이라 윈도우 기본 기능으로는 풀 수 없습니다.");
        Log(L"            반디집이나 7-Zip 같은 압축 프로그램이 필요합니다.");
    }
    return false;
}

// 다 받으셨는지 확인한다. 받는 중이면 브라우저가 파일을 잡고 있어 쓰기로 못 연다.
static bool DownloadFinished(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// 다운로드 폴더와 바탕화면의 큰 압축 파일 (최근 것부터). 이름은 따지지 않는다.
static std::vector<std::wstring> RecentArchives() {
    std::vector<std::pair<ULONGLONG, std::wstring> > v;
    wchar_t prof[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) {
        const wchar_t* dirs[] = { L"\\Downloads", L"\\Desktop" };
        const wchar_t* pats[] = { L"\\*.zip", L"\\*.7z", L"\\*.rar" };
        for (int di = 0; di < 2; di++) {
            std::wstring base = std::wstring(prof) + dirs[di];
            if (!DirExists(base)) continue;
            for (int e = 0; e < 3; e++) {
                WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((base + pats[e]).c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    LARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
                    if (sz.QuadPart < 40LL * 1024 * 1024) continue;
                    ULARGE_INTEGER t; t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                    t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                    v.push_back(std::make_pair(t.QuadPart, base + L"\\" + fd.cFileName));
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
    std::sort(v.begin(), v.end());
    std::reverse(v.begin(), v.end());
    std::vector<std::wstring> out;
    for (size_t i = 0; i < v.size(); i++) out.push_back(v[i].second);
    return out;
}

// ================================================================ 신경망 파일 찾기
static bool FindNeuralDll(const std::wstring& gameDir, std::wstring& out) {
    Log(L"신경망 파일(nvngx_dlssnr.dll)을 찾고 있습니다.");

    Log(L"  1) 이 프로그램이 있는 폴더를 봅니다.");
    {
        std::wstring dir = SelfDir() + L"\\" + DATA_DIR;
        const wchar_t* pick = NR_DLL;
        Log(std::wstring(L"     찾는 파일: ") + pick);
        std::wstring side = dir + L"\\" + pick;
        if (FileExists(side)) {
            Log(L"     → 프로그램과 같은 폴더에 있습니다. 이것을 씁니다.");
            out = side; return true;
        }
        side = dir + L"\\" + NR_DLL;
        if (FileExists(side)) {
            Log(L"     → 세대별 파일은 없지만 nvngx_dlssnr.dll 이 있습니다. 이것을 씁니다.");
            out = side; return true;
        }
    }

    Log(L"  2) 이 게임 폴더 안을 봅니다.");
    if (FileExists(gameDir + L"\\" + NR_DLL)) { out = gameDir + L"\\" + NR_DLL; return true; }

    Log(L"  3) 전에 찾아 둔 위치를 봅니다.");
    std::wstring saved = LoadSavedNr();
    if (!saved.empty()) { out = saved; return true; }

    Log(L"  4) 다른 게임 폴더를 봅니다.");
    Status(L"신경망 파일을 찾는 중... (다른 게임 폴더)");
    for (size_t i = 0; i < g_games.size(); i++) {
        std::wstring d = ResolveExeDir(g_games[i].dir);
        if (FileExists(d + L"\\" + NR_DLL)) { out = d + L"\\" + NR_DLL; return true; }
    }

    wchar_t prof[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) {
        Log(L"  5) 다운로드 폴더와 바탕화면을 봅니다.");
        Status(L"신경망 파일을 찾는 중... (다운로드 폴더)");
        if (FindFileDeep(std::wstring(prof) + L"\\Downloads", NR_DLL, 3, out)) return true;
        if (FindFileDeep(std::wstring(prof) + L"\\Desktop", NR_DLL, 2, out)) return true;
    }

    Log(L"  6) 엔비디아 드라이버 폴더를 봅니다.");
    Status(L"신경망 파일을 찾는 중... (드라이버 폴더)");
    if (FindFileDeep(L"C:\\Windows\\System32\\DriverStore\\FileRepository", NR_DLL, 2, out)) return true;
    if (FindFileDeep(L"C:\\ProgramData\\NVIDIA", NR_DLL, 4, out)) return true;

    Log(L"  7) 다운로드 폴더에 받아 둔 압축 파일 안을 봅니다.");
    if (ExtractFromArchives(out)) return true;

    Log(L"  → 어디에도 없습니다. 하드디스크 전체 검색은 하지 않습니다.");
    Log(L"     이 파일은 받으신 압축 안에 프로그램과 함께 들어 있습니다.");
     
    Log(L"     압축을 푸신 폴더에서 실행하시면 1번 단계에서 바로 찾습니다.");
    return false;
}

// ================================================================ 설치 / 되돌리기
struct Job { std::wstring dir; bool restore; };

static bool HasFxFiles(const std::wstring& dir, int depth) {
    if (depth < 0 || !DirExists(dir)) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.fx").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { FindClose(h); return true; }
    h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (HasFxFiles(dir + L"\\" + n, depth - 1)) { found = true; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// 리세이드 프리셋에 필요한 효과 두 개를 켠다.
// 모션 벡터를 만들어 주는 효과가 꺼져 있으면 신경망이 움직임 정보를 못 받는다.
// 순서도 중요하다. 모션 벡터가 DLSS5_Feed 보다 앞에 와야 한다.
// 되돌릴 때 프리셋에서 우리가 켠 효과 둘만 뺀다. 사용자가 켜 둔 것은 그대로 둔다.
//
// 2026-10-23 매드맥스에서 찾았다. 되돌린 뒤 효과 파일(DLSS5_Feed.fx, dh_uber_motion.fx)은
// 지워지는데 프리셋은 그 이름을 계속 불러서, 다음에 리세이드 창을 열면 없는 효과를
// 찾느라 오류가 난다. 넣을 때 켰으면 뺄 때 꺼야 한다.
// 되돌릴 때 효과 목록에서 우리 것만 뺀다.
//
// 2026-10-24. 전에는 DH_UBER_MOTION_020 을 무조건 뺐다. 그런데 설치 쪽은
// 공급원이 하나도 없을 때만 그것을 넣는다 (InsertFeedInto). 그래서 원래
// dh_uber_motion 을 모션벡터 공급원으로 쓰시던 분은 되돌리기 한 번에
// 자기 설정이 사라진다. 우리가 넣은 것일 때만 뺀다.
// 우리가 넣었으면 바로 뒤에 DLSS5_Feed 가 붙어 있다. 그것을 표시로 쓴다.
//
// TechniqueSorting 은 건드리지 않는다. 그 줄은 효과를 켜고 끄는 줄이 아니라
// 순서를 적어 두는 줄이다. 거기서 이름을 빼면 사용자가 맞춰 둔 순서가 흐트러진다.
static bool DisableFeedTechniques(const std::wstring& presetPath) {
    const char NL = (char)10;
    const char* FEED = "DLSS5_Feed@DLSS5_Feed.fx";
    const char* DHU  = "DH_UBER_MOTION_020@dh_uber_motion.fx";
    std::string t = ReadFileBytes(presetPath);
    if (t.empty()) { return false; }
    bool touched = false;
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= t.size()) {
        size_t nl = t.find(NL, start);
        if (nl == std::string::npos) { lines.push_back(t.substr(start)); break; }
        lines.push_back(t.substr(start, nl - start + 1));
        start = nl + 1;
    }
    for (size_t i = 0; i < lines.size(); i++) {
        const char* KEY = "Techniques=";
        size_t kl = strlen(KEY);
        if (lines[i].compare(0, kl, KEY) != 0) { continue; }
        std::string rest = lines[i].substr(kl);
        std::string tail;
        while (!rest.empty() && (rest[rest.size() - 1] == (char)13
                              || rest[rest.size() - 1] == (char)10)) {
            tail = std::string(1, rest[rest.size() - 1]) + tail;
            rest.erase(rest.size() - 1);
        }
        std::vector<std::string> items;
        size_t p = 0;
        while (p <= rest.size()) {
            size_t c = rest.find(',', p);
            std::string one = (c == std::string::npos) ? rest.substr(p) : rest.substr(p, c - p);
            if (!one.empty()) { items.push_back(one); }
            if (c == std::string::npos) { break; }
            p = c + 1;
        }
        std::vector<std::string> keep;
        for (size_t j = 0; j < items.size(); j++) {
            if (items[j] == FEED) { touched = true; continue; }
            // 우리가 넣은 dh_uber_motion 인가. 바로 뒤에 DLSS5_Feed 가 붙어 있으면 우리 것이다.
            if (items[j] == DHU && j + 1 < items.size() && items[j + 1] == FEED) {
                touched = true;
                continue;
            }
            keep.push_back(items[j]);
        }
        std::string joined;
        for (size_t j = 0; j < keep.size(); j++) {
            if (j > 0) { joined += ","; }
            joined += keep[j];
        }
        lines[i] = std::string(KEY) + joined + tail;
    }
    if (!touched) { return false; }
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) { out += lines[i]; }
    return WriteFileBytes(presetPath, out);
}

// 이 효과가 모션벡터를 만들어 주는 것인가.
//
// DLSS5_Feed 는 공급원이 자기보다 앞에 있어야 한다. DLSS5_Feed.fx 머리말에
// 그대로 적혀 있다 — "enable that provider's technique ABOVE this one in the
// effect list". 뒤에 있으면 피드가 지난 프레임의 모션벡터를 읽는다.
//
// 2026-10-24 데이즈곤에서 실제로 났다. 되돌리기가 DLSS5_Feed 를 목록에서 빼면
// Lumenite_Kernel, AdaptiveSharpen, FilmGrain2 만 남는데, 다시 설치할 때
// 우리 둘을 맨 앞에 끼워 넣어서 Lumenite_Kernel 이 피드 뒤로 밀렸다.
// 카메라를 돌리면 배경이 한 박자 늦게 따라왔다.
static bool IsMvProvider(const std::string& item) {
    const char* names[] = { "lumenite_kernel.fx", "lumenite_quantmotion.fx",
                            "martysmods_launchpad.fx", "vort_motion.fx",
                            "quint_motionvectors.fx", "dh_uber_motion.fx" };
    std::string low = LowerA(item);
    for (int i = 0; i < 6; i++) {
        if (low.find(names[i]) != std::string::npos) { return true; }
    }
    return false;
}

// 효과 목록 한 줄에 DLSS5_Feed 를 끼워 넣는다.
//
// 이미 모션벡터 공급원이 켜져 있으면 그 뒤에 넣는다. 공급원을 하나 더 넣지
// 않는다. 사용자가 고른 공급원이 따로 있는데 dh_uber_motion 을 같이 켜면
// 하는 일 없이 프레임만 먹는다.
// 공급원이 하나도 없을 때만 dh_uber_motion 을 같이 맨 앞에 넣는다.
//
// 2026-10-24 매드맥스에서 걸렸다. 이 함수를 TechniqueSorting 줄에도 썼더니
// 거기는 셰이더 549개가 전부 적힌 목록이라, 안 켠 공급원(vort_Motion, 527번)
// 뒤에 피드를 꽂아 528번에 놨다. 공급원도 "이미 있다"로 보고 안 넣었다.
// 순서 규칙은 켠 효과 목록(Techniques)에만 쓴다.
// TechniqueSorting 은 켜고 끄는 줄이 아니라 전체 순서를 적어 두는 줄이라,
// 거기엔 빠진 이름만 앞에 붙이고 자리를 옮기지 않는다 (sortLine = true).
static std::string InsertFeedInto(const std::string& body, bool sortLine) {
    const char* FEED = "DLSS5_Feed@DLSS5_Feed.fx";
    const char* DHU  = "DH_UBER_MOTION_020@dh_uber_motion.fx";
    std::vector<std::string> items;
    size_t p = 0;
    while (p <= body.size()) {
        size_t c = body.find(',', p);
        std::string it = (c == std::string::npos) ? body.substr(p) : body.substr(p, c - p);
        if (!it.empty()) { items.push_back(it); }
        if (c == std::string::npos) { break; }
        p = c + 1;
    }
    // 전체 순서 목록은 자리를 옮기지 않는다. 빠진 이름만 앞에 붙인다.
    if (sortLine) {
        std::vector<std::string> head;
        bool hasDhu = false, hasFeed = false;
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i] == DHU)  { hasDhu = true; }
            if (items[i] == FEED) { hasFeed = true; }
        }
        if (!hasDhu)  { head.push_back(DHU); }
        if (!hasFeed) { head.push_back(FEED); }
        std::string res;
        for (size_t i = 0; i < head.size(); i++) { res += head[i] + ","; }
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) { res += ","; }
            res += items[i];
        }
        return res;
    }

    // 이미 들어 있는 DLSS5_Feed 는 걷어낸다. 자리를 다시 잡아야 한다.
    for (size_t i = items.size(); i > 0; i--) {
        if (items[i - 1] == FEED) { items.erase(items.begin() + (i - 1)); }
    }
    int last = -1;
    for (size_t i = 0; i < items.size(); i++) {
        if (IsMvProvider(items[i])) { last = (int)i; }
    }
    std::vector<std::string> out;
    if (last >= 0) {
        for (size_t i = 0; i < items.size(); i++) {
            out.push_back(items[i]);
            if ((int)i == last) { out.push_back(FEED); }
        }
    } else {
        out.push_back(DHU);
        out.push_back(FEED);
        for (size_t i = 0; i < items.size(); i++) { out.push_back(items[i]); }
    }
    std::string res;
    for (size_t i = 0; i < out.size(); i++) {
        if (i > 0) { res += ","; }
        res += out[i];
    }
    return res;
}

// 프리셋 한 칸에 값을 써 넣는다.
//
// SetIniKey 를 쓰지 않는 까닭은 그쪽이 "key = value" 로 띄어 쓰기 때문이다.
// 리세이드는 자기가 쓸 때 "key=value" 로 적는다. 읽기는 둘 다 되지만
// 한 파일 안에서 모양이 섞이면 나중에 보기 나쁘다.
// 칸이 없으면 그 자리에 넣고, 섹션이 없으면 파일 끝에 섹션째 만든다.
static void SetPresetValue(std::string& text, const char* sec,
                           const char* key, const char* val) {
    const char NL = (char)10;
    std::string crlf; crlf += (char)13; crlf += (char)10;
    std::string want = std::string("[") + sec + "]";
    std::string wantKey = key;

    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(NL, start);
        if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
        lines.push_back(text.substr(start, nl - start + 1));
        start = nl + 1;
    }

    bool inSec = false;
    int secEnd = -1;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = lines[i];
        while (!t.empty() && (t[t.size() - 1] == (char)13 || t[t.size() - 1] == (char)10)) {
            t.erase(t.size() - 1);
        }
        if (!t.empty() && t[0] == '[') {
            if (inSec) { secEnd = (int)i; break; }
            inSec = (t == want);
            continue;
        }
        if (!inSec) { continue; }
        size_t eq = t.find('=');
        if (eq == std::string::npos) { continue; }
        if (t.substr(0, eq) == wantKey) {
            lines[i] = wantKey + "=" + val + crlf;
            text.clear();
            for (size_t j = 0; j < lines.size(); j++) { text += lines[j]; }
            return;
        }
    }

    if (!inSec) {
        // 섹션이 아예 없다. 파일 끝에 만든다.
        if (!text.empty() && text[text.size() - 1] != (char)10) { text += crlf; }
        text += crlf + want + crlf + wantKey + "=" + val + crlf;
        return;
    }
    // 섹션은 있는데 칸이 없다. 섹션 끝에 넣는다.
    if (secEnd < 0) { secEnd = (int)lines.size(); }
    lines.insert(lines.begin() + secEnd, wantKey + "=" + val + crlf);
    text.clear();
    for (size_t j = 0; j < lines.size(); j++) { text += lines[j]; }
}

// 프리셋이 비어 있는가.
//
// 파일이 없거나, 파일은 있는데 켜 둔 효과가 하나도 없으면 비어 있는 것이다.
// 리세이드는 한 번 돌고 나면 효과를 안 켜도 파일을 만들어 두기 때문에,
// "파일이 있나" 만 보면 비어 있는 프리셋까지 남의 것으로 보게 된다.
//
// 보이는 효과(AdaptiveSharpen, FilmGrain2)는 이 경우에만 넣는다.
// 하나라도 켜 두셨으면 맞춰 두신 화면이니 손대지 않는다. (2026-10-24)
static bool PresetIsEmpty(const std::wstring& path) {
    std::string t = ReadFileBytes(path);
    if (t.empty()) { return true; }
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find((char)10, p);
        std::string ln = t.substr(p, (e == std::string::npos ? t.size() : e) - p);
        p = (e == std::string::npos) ? t.size() : e + 1;
        while (!ln.empty() && (ln[ln.size() - 1] == (char)13 || ln[ln.size() - 1] == ' ')) {
            ln.erase(ln.size() - 1);
        }
        if (ln.compare(0, 11, "Techniques=") != 0) { continue; }
        if (ln.size() > 11) { return false; }   // 켜 둔 것이 있다
    }
    return true;
}

// 눈에 보이는 기본 효과를 프리셋에 켜 준다.
//
// 2026-10-24. 그동안 리세이드를 넣기만 하고 켜 주는 효과가 하나도 없었다.
// 깔고 나서 숫자 8 을 눌러도 목록만 보이고, F9 를 눌러도 아무 일이 없었다.
// 받으신 분이 리세이드가 뭘 해 주는지 알 길이 없다.
//
// 넣는 것은 둘뿐이다.
//
// 처음에는 데이즈곤 값(curve_height 0.70, grainamount 0.05)을 그대로 가져왔다.
// 그런데 라오어 1에서 직접 켜 보니 "있는지 없는지 모르겠다" 수준이었다.
// 그 값은 신경망이 이미 세게 걸린 위에 살짝 얹는 용도라 낮게 잡은 것이었다.
// 라오어 1에서 올려 가며 맞춘 값이 아래다. F9 로 껐다 켜면 분명히 보인다.
//   AdaptiveSharpen  curve_height 1.20. 셰이더 기본값 1.0 보다 위다. 범위는 0.01~2.0.
//                    테두리가 하얗게 뜨는 것은 overshoot 값(L 0.003 / D 0.009)이 막는다.
//                    그 둘은 건드리지 않으므로 세게 걸어도 할로는 안 생긴다.
//   FilmGrain2       grainamount 0.10. 범위 0~0.2 의 절반이다. 기본값 0.05 는 사실상 최소다.
//                    coloramount 0.25. 기본 0.6 에서 내렸다. 실제 센서 노이즈는 거의 휘도 쪽이다.
//                    grainsize 1.60 (셰이더 기본값). 데이즈곤에서는 2.00 을 쓰는데
//                    그건 4K 영상 압축에서 살아남게 고른 값이다. 크기는 픽셀 단위라
//                    해상도가 낮을수록 화면에서 더 굵게 보인다. 모든 게임에 주는 값이라 1.60 으로 둔다.
//
// 색을 건드리는 효과(Tonemap, LiftGammaGain, Vibrance 같은 것)는 넣지 않는다.
// 게임 전용 비주얼 모드와 두 번 겹치고, HDR 로 하시는 분 화면이 깨진다.
// Vignette 과 MXAO 도 맞춰 보고 뺀 것들이다.
//
// 목록 맨 뒤에 붙인다. 공급원과 피드가 앞에 있어야 하기 때문이다.
// 사용자가 이미 켜 둔 것이면 건드리지 않고, 값도 이미 적혀 있으면 덮지 않는다.
static bool AddLookTechniques(const std::wstring& presetPath) {
    const char NL = (char)10;
    const char* SHARP = "AdaptiveSharpen@AdaptiveSharpen.fx";
    const char* GRAIN = "FilmGrain2@FilmGrain2.fx";
    std::string crlf; crlf += (char)13; crlf += (char)10;
    std::string t = ReadFileBytes(presetPath);

    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= t.size()) {
        size_t nl = t.find(NL, start);
        if (nl == std::string::npos) { lines.push_back(t.substr(start)); break; }
        lines.push_back(t.substr(start, nl - start + 1));
        start = nl + 1;
    }

    bool touched = false;
    // 우리가 켠 효과인가. 값은 우리가 켠 것에만 쓴다.
    bool addSharp = false;
    bool addGrain = false;
    for (size_t i = 0; i < lines.size(); i++) {
        const char* keys[] = { "Techniques=", "TechniqueSorting=" };
        for (int k = 0; k < 2; k++) {
            size_t kl = strlen(keys[k]);
            if (lines[i].compare(0, kl, keys[k]) != 0) { continue; }
            std::string rest = lines[i].substr(kl);
            std::string tail;
            while (!rest.empty() && (rest[rest.size() - 1] == (char)13
                                  || rest[rest.size() - 1] == (char)10)) {
                tail = std::string(1, rest[rest.size() - 1]) + tail;
                rest.erase(rest.size() - 1);
            }
            const char* want[] = { SHARP, GRAIN };
            for (int w = 0; w < 2; w++) {
                if (rest.find(want[w]) != std::string::npos) { continue; }
                if (!rest.empty()) { rest += ","; }
                rest += want[w];
                touched = true;
                // 켠 효과 줄에서만 표시한다. 그래야 "우리가 켠 것" 인지 안다.
                if (k == 0) {
                    if (w == 0) { addSharp = true; }
                    else { addGrain = true; }
                }
            }
            lines[i] = std::string(keys[k]) + rest + tail;
        }
    }

    std::string out;
    for (size_t i = 0; i < lines.size(); i++) { out += lines[i]; }

    // 줄이 아예 없는 프리셋이면 새로 만든다.
    if (out.find("Techniques=") == std::string::npos) {
        std::string head;
        head += std::string("Techniques=") + SHARP + "," + GRAIN + crlf;
        head += std::string("TechniqueSorting=") + SHARP + "," + GRAIN + crlf;
        out = out.empty() ? head : (out + crlf + head);
        touched = true;
        addSharp = true;
        addGrain = true;
    }

    if (!touched) { return true; }

    // 값은 우리가 켠 효과에만 쓴다.
    //
    // 2026-10-24 붉은사막에서 걸렸다. 처음에는 "칸이 이미 있으면 안 덮는다" 로
    // 만들었는데, 그 게임에는 프리셋이 이미 있었고 두 효과의 칸이 리세이드
    // 기본값인 채로 들어 있었다. 그래서 저희 값이 하나도 안 들어가고
    // 샤픈이 1.0 으로 세게 걸렸다. 테두리가 하얗게 뜨고 그레인은 알록달록해진다.
    //   curve_height 1.000000   coloramount 0.600000   grainsize 1.600000
    // 칸이 있다고 맞춰 둔 값인 것은 아니다. 리세이드는 안 쓰는 효과의 기본값도
    // 파일에 적어 둔다. 그래서 "칸이 있나" 가 아니라 "우리가 켰나" 로 가린다.
    // 원래 켜 두셨던 효과면 그분이 보시던 값이니 손대지 않는다.
    // 값은 data\reshade\look_values.ini 에서 읽는다.
    //
    // 2026-10-24. 전에는 여기에 숫자 15개를 박아 두었다. 그러면 값 하나를
    // 바꿀 때마다 프로그램을 다시 만들어야 한다. 오늘만 두 번 바꿨다.
    // 파일로 빼 두면 그 파일의 숫자만 고치면 된다.
    // 파일이 없으면 값을 안 쓴다. 효과는 켜지되 셰이더 기본값으로 돈다.
    {
        std::string vals = ReadFileBytes(SelfDir() + L"\\" + DATA_DIR + L"\\reshade\\look_values.ini");
        std::string sec;
        size_t vp = 0;
        while (vp < vals.size()) {
            size_t e = vals.find((char)10, vp);
            std::string ln = vals.substr(vp, (e == std::string::npos ? vals.size() : e) - vp);
            vp = (e == std::string::npos) ? vals.size() : e + 1;
            while (!ln.empty() && (ln[ln.size() - 1] == (char)13 || ln[ln.size() - 1] == ' ')) {
                ln.erase(ln.size() - 1);
            }
            if (ln.empty() || ln[0] == ';') { continue; }
            if (ln[0] == '[') {
                size_t rb = ln.find(']');
                sec = (rb == std::string::npos) ? "" : ln.substr(1, rb - 1);
                continue;
            }
            size_t eq = ln.find('=');
            if (eq == std::string::npos) { continue; }
            bool want = (sec == "AdaptiveSharpen.fx" && addSharp)
                     || (sec == "FilmGrain2.fx"      && addGrain);
            if (!want) { continue; }
            SetPresetValue(out, sec.c_str(), ln.substr(0, eq).c_str(), ln.substr(eq + 1).c_str());
        }
    }
    return WriteFileBytes(presetPath, out);
}

static bool EnableFeedTechniques(const std::wstring& presetPath) {
    const char* WANT = "DH_UBER_MOTION_020@dh_uber_motion.fx,DLSS5_Feed@DLSS5_Feed.fx";
    const char NL = (char)10;
    std::string t = ReadFileBytes(presetPath);

    // 파일이 없거나 비어 있으면 새로 만든다.
    if (t.find("Techniques=") == std::string::npos) {
        std::string crlf; crlf += (char)13; crlf += (char)10;
        std::string out;
        out += std::string("Techniques=") + WANT + crlf;
        out += std::string("TechniqueSorting=") + WANT + crlf;
        out += crlf;
        out += std::string("[dh_uber_motion.fx]") + crlf;
        out += crlf;
        out += std::string("[DLSS5_Feed.fx]") + crlf;
        if (!t.empty()) out = t + crlf + out;
        return WriteFileBytes(presetPath, out);
    }

    // 이미 켜져 있으면 그대로 둔다.
    //
    // 2026-10-24. 전에는 파일 전체에서 찾았다. 그러면 TechniqueSorting 에만
    // 이름이 남아 있고 Techniques 에는 없는 경우까지 "켜져 있다"로 보고
    // 그냥 돌아가서, 효과가 영영 안 켜진다. Techniques 줄만 본다.
    {
        size_t tp = t.find("Techniques=");
        // TechniqueSorting= 도 "Techniques=" 를 품고 있으니 줄머리인 것만 고른다.
        while (tp != std::string::npos && tp != 0 && t[tp - 1] != (char)10 && t[tp - 1] != (char)13) {
            tp = t.find("Techniques=", tp + 1);
        }
        if (tp != std::string::npos) {
            size_t te = t.find(NL, tp);
            std::string line = t.substr(tp, (te == std::string::npos ? t.size() : te) - tp);
            if (line.find("DLSS5_Feed@DLSS5_Feed.fx") != std::string::npos) { return true; }
        }
    }

    // 목록에 끼워 넣는다. 사용자가 켜 둔 효과는 그대로 둔다.
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= t.size()) {
        size_t nl = t.find(NL, start);
        if (nl == std::string::npos) { lines.push_back(t.substr(start)); break; }
        lines.push_back(t.substr(start, nl - start + 1));
        start = nl + 1;
    }
    for (size_t i = 0; i < lines.size(); i++) {
        const char* keys[] = { "Techniques=", "TechniqueSorting=" };
        for (int k = 0; k < 2; k++) {
            size_t kl = strlen(keys[k]);
            if (lines[i].compare(0, kl, keys[k]) != 0) continue;
            std::string rest = lines[i].substr(kl);
            while (!rest.empty() && (rest[rest.size()-1] == (char)13 || rest[rest.size()-1] == (char)10))
                rest.erase(rest.size() - 1);
            std::string crlf; crlf += (char)13; crlf += (char)10;
            // k==1 이 TechniqueSorting 줄이다. 거기는 자리를 안 옮긴다.
            lines[i] = std::string(keys[k]) + InsertFeedInto(rest, k == 1) + crlf;
        }
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += lines[i];
    return WriteFileBytes(presetPath, out);
}
// 설치가 끝난 뒤 스스로 점검한다.
// 몇만 명이 쓰는데 로그를 사람이 일일이 볼 수는 없다. 빠진 것이 있으면 창으로 알려 준다.
// 반환값: 빠진 항목 수
static int SelfCheck(const std::wstring& gameDir, bool modeB, std::wstring& reportOut) {
    std::wstring NL(1, (wchar_t)10);
    std::wstring bad;
    int miss = 0;

    // 어느 모드든 신경망 파일은 있어야 한다
    std::wstring nr = gameDir + L"\\" + NR_DLL;
    if (!FileExists(nr)) {
        bad += L"  - 신경망 파일(nvngx_dlssnr.dll) 이 없습니다" + NL; miss++;
    } else if (FileSizeOf(nr) < 100LL * 1024 * 1024) {
        bad += L"  - 신경망 파일이 너무 작습니다. 복사가 덜 됐습니다" + NL; miss++;
    }

    if (modeB) {
        if (!FileExists(gameDir + L"\\" + FEED_ADDON))
            { bad += L"  - DLSS5-Feeder 애드온이 없습니다" + NL; miss++; }
        if (!FileExists(gameDir + L"\\" + RENODX_ADDON))
            { bad += L"  - RenoDX 신경망 애드온이 없습니다" + NL; miss++; }
        std::wstring dr = gameDir + L"\\" + DLSS_DLL;
        if (!FileExists(dr))
            { bad += L"  - DLSS 런타임(nvngx_dlss.dll) 이 없습니다" + NL; miss++; }
        else if (FileSizeOf(dr) < 10LL * 1024 * 1024)
            { bad += L"  - DLSS 런타임이 너무 작습니다. 복사가 덜 됐습니다" + NL; miss++; }
        if (!FileExists(gameDir + L"\\host64\\" + FEED_HOST))
            { bad += L"  - Feeder 보조 실행 파일이 없습니다" + NL; miss++; }
        if (!FileExists(gameDir + L"\\reshade-shaders\\Shaders\\" + FEED_FX))
            { bad += L"  - DLSS5_Feed.fx 셰이더가 없습니다" + NL; miss++; }

        // 리세이드 본체
        const wchar_t* hooks[] = { L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"d3d9.dll", L"opengl32.dll" };
        bool rs = false, rsAddon = false;
        for (int i = 0; i < 5; i++) {
            std::wstring p = gameDir + L"\\" + hooks[i];
            if (FileExists(p) && IsReShadeFile(p)) { rs = true; rsAddon = IsReShadeAddonBuild(p); break; }
        }
        if (!rs) { bad += L"  - 리세이드가 없습니다" + NL; miss++; }
        else if (!rsAddon) { bad += L"  - 리세이드가 애드온을 지원하지 않는 버전입니다" + NL; miss++; }

        // 모션 벡터 효과가 프리셋에서 켜져 있는지
        std::string pr = ReadFileBytes(gameDir + L"\\" + RS_PRESET);
        if (pr.find("DLSS5_Feed@DLSS5_Feed.fx") == std::string::npos)
            { bad += L"  - 프리셋에 DLSS5_Feed 효과가 꺼져 있습니다" + NL; miss++; }
        if (pr.find("DH_UBER_MOTION_020") == std::string::npos)
            { bad += L"  - 프리셋에 모션 벡터 효과가 꺼져 있습니다" + NL; miss++; }
        if (!HasFxFiles(gameDir + L"\\reshade-shaders", 3))
            { bad += L"  - 리세이드 효과 파일(.fx)이 없습니다" + NL; miss++; }
        std::string ri = ReadFileBytes(gameDir + L"\\" + RS_INI);
        if (ri.find("NRStyle=2") != std::string::npos)
            { bad += L"  - NR Style 이 2 입니다. 이대로면 다음에 게임이 안 켜집니다" + NL; miss++; }
    } else {
        if (!FileExists(gameDir + L"\\OptiScaler.ini"))
            { bad += L"  - OptiScaler.ini 가 없습니다" + NL; miss++; }
        if (!DirExists(gameDir + L"\\OptiScaler"))
            { bad += L"  - OptiScaler 폴더가 없습니다" + NL; miss++; }
        if (!FileExists(gameDir + L"\\nvngx.dll_dlssnr.dll"))
            { bad += L"  - nvngx.dll_dlssnr.dll 이 없습니다" + NL; miss++; }
        // 프록시 dll (winmm / dxgi / version 중 하나가 OptiScaler 여야 한다)
        const wchar_t* px[] = { L"winmm.dll", L"dxgi.dll", L"version.dll", L"dbghelp.dll",
                                L"wininet.dll", L"winhttp.dll" };
        bool found = false;
        for (int i = 0; i < 6; i++)
            if (IsOptiScalerFile(gameDir + L"\\" + px[i])) { found = true; break; }
        if (!found) { bad += L"  - OptiScaler 본체가 게임 폴더에 없습니다" + NL; miss++; }
    }
    reportOut = bad;
    return miss;
}
// 깃허브에서 우리가 동봉한 것과 같은 파일의 주소를 받아온다.
// 주소에 버전을 박아 두면 릴리스가 지워졌을 때 죽는다. 실패하면 박아 둔 주소로 돌아간다.
//
// 2026-10-22. 예전에는 "최신 릴리스"를 받아왔다. 이제 그런게 하지 않는다.
// 포크는 일주일에도 몇 판씩 나오고, 판본이 바니면 ini 항목 이름이 바뀔 수 있다.
// 그러면 우리가 넣는 기본값이 안 먹는다. 전수조사를 마친 판본만 쓴다.
static std::wstring FetchLatestOptiUrl() {
    std::wstring tmp = TempDir() + L"release.json";
    DeleteFileW(tmp.c_str());
    const wchar_t* api =
        L"https://api.github.com/repos/janblade/OptiScaler-F5-DLSSNR-Multipass/releases";
    if (FAILED(URLDownloadToFileW(NULL, api, tmp.c_str(), 0, NULL))) return L"";
    std::string j = ReadFileBytes(tmp);
    DeleteFileW(tmp.c_str());
    if (j.empty()) return L"";

    // "browser_download_url": "...zip" 중 .zip 으로 끝나는 첫 항목
    const std::string key = "\"browser_download_url\"";
    size_t p = 0;
    while ((p = j.find(key, p)) != std::string::npos) {
        size_t c = j.find(':', p + key.size());
        if (c == std::string::npos) break;
        size_t q1 = j.find('"', c);
        if (q1 == std::string::npos) break;
        size_t q2 = j.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string u = j.substr(q1 + 1, q2 - q1 - 1);
        p = q2;
        // 동봉한 것과 같은 파일 이름일 때만 쓴다.
        if (u.find("OptiScaler-DLSSNR-F5-0.1.33-with-dlss-fg.zip") != std::string::npos)
            return U2W(u);
    }
    return L"";
}
// 신경망이 돌 해상도를 화면 크기에 맞춰 정한다.
// 고정값을 쓰면 4K 에서는 감당이 안 되고, 1440p 이하에서는 쓸데없이 흐려진다.
// 목표는 대략 260만 화소(=2172x1222 쯤) 언저리에서 도는 것이다.
static int PickWorkResolution(int& wOut, int& hOut) {
    // 2026-10-20. GetSystemMetrics 는 윈도우 화면 배율이 걸려 있으면 줄어든 값을 준다.
    // 대표님 4K 화면에 150% 배율이 걸려 있어 2560x1440 으로 읽혔고,
    // 그래서 "1440p 이하"로 판단해 처리 해상도를 100% 로 정했다.
    // 4K 100% 는 33fps 다. 받으신 분이 배율을 쓰면 처음부터 그 상태로 시작한다.
    //   3840 / 1.5 = 2560     2160 / 1.5 = 1440
    // EnumDisplaySettings 는 배율과 무관하게 실제 화소 수를 준다.
    int w = 0;
    int h = 0;
    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        w = (int)dm.dmPelsWidth;
        h = (int)dm.dmPelsHeight;
    }
    if (w <= 0 || h <= 0) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }
    if (w <= 0 || h <= 0) { w = 1920; h = 1080; }
    wOut = w; hOut = h;

    double px = (double)w * (double)h;
    const double BUDGET = 3700000.0;   // 실측: 4K 67%(372만 화소)에서 47fps, 1440p 67%(165만)에서 60fps
    if (px <= BUDGET) return 100;      // 1440p 이하는 그대로 돌린다

    // 넓이 기준이므로 제곱근을 취한다. 5% 단위로 내림하고 50 아래로는 내리지 않는다.
    double r = sqrt(BUDGET / px) * 100.0;
    int pct = (int)(r / 5.0) * 5;
    if (pct < 50) pct = 50;
    if (pct > 100) pct = 100;
    return pct;
}
// a 가 b 보다 새 파일인가. 둘 중 하나라도 없으면 거짓이다.
static bool IsNewerThan(const std::wstring& a, const std::wstring& b) {
    WIN32_FILE_ATTRIBUTE_DATA fa, fb;
    if (!GetFileAttributesExW(a.c_str(), GetFileExInfoStandard, &fa)) return false;
    if (!GetFileAttributesExW(b.c_str(), GetFileExInfoStandard, &fb)) return false;
    return CompareFileTime(&fa.ftLastWriteTime, &fb.ftLastWriteTime) > 0;
}

// 실행 파일 안에 그 문자열이 있는지 본다. DX11 인지 DX12 인지 가리는 데 쓴다.
static bool ExeHasString(const std::wstring& path, const char* needle) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string want(needle);
    std::vector<char> buf(1 << 20);
    std::string tail; DWORD got = 0; bool hit = false;
    while (!hit && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail;
        for (DWORD i = 0; i < got; i++) {
            char c = buf[i];
            if (c == 0) continue;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            chunk.push_back(c);
        }
        if (chunk.find(want) != std::string::npos) hit = true;
        tail = (chunk.size() > 32) ? chunk.substr(chunk.size() - 32) : chunk;
    }
    CloseHandle(h);
    return hit;
}

// ── 스트림라인 게임에서 프레임 생성이 돌게 해 준다.
// OptiScaler 가 DLSSG 를 출력으로 쓰려면 sl.*.dll 을 자기 폴더 안에 따로
// 들고 있어야 한다. 없으면 게임 안에서
// "Can't init DLSSG Output / Are you missing the streamline folder?" 가 뜨고
// 프레임 생성이 아예 안 걸린다. OptiScaler.ini 에도 적혀 있다 —
//   dlssg - requires streamline dlls inside 'OptiScaler/streamline' folder
// 게임 폴더의 것을 복사만 한다. 원본은 건드리지 않는다.
// 2026-10-15 위처3 리마스터 + RTX 5070 Ti 에서 확인. 43 → 78 프레임.
static void SetupStreamlineForFg(const std::wstring& gameDir,
                                 std::vector<std::wstring>& added) {
    if (!FileExists(gameDir + L"\\sl.interposer.dll")) return;
    if (!FileExists(gameDir + L"\\nvngx_dlssg.dll")) {
        Log(L"  스트림라인 게임인데 nvngx_dlssg.dll 이 없습니다. 프레임 생성 준비는 건너뜁니다.");
        return;
    }
    CreateDirectoryW((gameDir + L"\\OptiScaler").c_str(), NULL);
    std::wstring dir = gameDir + L"\\OptiScaler\\streamline";
    CreateDirectoryW(dir.c_str(), NULL);

    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((gameDir + L"\\sl.*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            if (CopyFileW((gameDir + L"\\" + name).c_str(),
                          (dir + L"\\" + name).c_str(), FALSE)) {
                added.push_back(std::wstring(L"OptiScaler\\streamline\\") + name);
                n++;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n == 0) {
        Log(L"  스트림라인 파일을 복사하지 못했습니다. 프레임 생성 준비는 건너뜁니다.");
        return;
    }
    wchar_t b[200];
    swprintf_s(b, L"  프레임 생성 준비: 스트림라인 파일 %d개를 넣었습니다.", n);
    Log(b);

    // ini 에 FGInput/FGOutput 을 써 넣지 않는다.
    // 1.0.1.1 에서 dlssg 로 써 넣었더니 "매니저로 잘 쓰던 위쳐3가 켜자마자 튕긴다"는
    // 제보가 왔다. 창에서 손으로 고르면 게임이 다 뜬 뒤라 실패해도 에러 글만 뜨는데,
    // ini 에 있으면 시작하는 도중에 시도해서 창을 띄울 기회도 없이 꺼진다.
    // 파일만 제자리에 놓고, 켜는 건 쓰실 분이 창에서 하도록 둔다.
    // 2026-10-15 제보. 1.0.1.2 에서 뺐다.

    // 1.0.1.1 로 이미 설치하신 분은 ini 에 그 값이 박혀 있다. 되돌리기는 ini 를
    // 사용자 설정으로 보고 안 건드리므로, 다시 설치해도 그대로 남아 계속 튕긴다.
    // 그래서 우리가 썼던 값과 똑같을 때만 기본값으로 돌려놓는다.
    // 직접 dlssg 로 바꿔 쓰시던 분의 값까지 건드리게 되지만, 창에서 한 번만
    // 다시 고르면 되는 일이라 튕기는 쪽을 막는 게 낫다.
    std::wstring ini = gameDir + L"\\OptiScaler.ini";
    std::string fin  = GetIniKey(ini, "FrameGen", "FGInput");
    std::string fout = GetIniKey(ini, "FrameGen", "FGOutput");
    if (LowerA(fin) == "dlssg" && LowerA(fout) == "dlssg") {
        SetIniKey(ini, "FrameGen", "FGInput",  "auto");
        SetIniKey(ini, "FrameGen", "FGOutput", "auto");
        Log(L"  1.0.1.1 이 넣었던 프레임 생성 설정을 기본값으로 되돌렸습니다.");
        Log(L"  그 설정 때문에 게임이 켜자마자 꺼지는 제보가 있었습니다.");
    }
}

// ── A 모드(옵티스케일러)에도 리세이드를 같이 넣는다.
// dxgi.dll 은 옵티스케일러가 쓰고 있으므로 비어 있는 다른 이름을 골라야 한다.
// 리세이드는 그래픽 API 이름으로만 들어간다. version.dll 이나 winmm.dll 은 못 쓴다.
// 이걸 안 해 주면, 나중에 리세이드를 넣으려는 사람이
// "dxgi.dll already exists, but does not belong to ReShade" 에서 막히고
// 모르고 덮어쓰면 DLSS 5 가 통째로 날아간다. (2026-10-09 위처3에서 확인)
// 전에 우리가 넣어둔 리세이드를 치운다.
//
// 2026-10-18. 리세이드를 안 넣기로 한 경우(매니저가 들고 있거나, 창에서 끄셨거나)
// 새로 안 넣는 것만으로는 부족하다. 전에 넣어둔 것이 그대로 남아 돌기 때문이다.
// 레데리2에서 두 벌이 올라와 나중 것이 스스로 꺼지는 것을 확인했다.
// 우리가 넣은 것인지 확인하고 지운다. 남의 것은 건드리지 않는다.
static void RemoveOurReShade(const std::wstring& gameDir) {
    const wchar_t* names[] = { L"d3d11.dll", L"d3d12.dll", L"dxgi.dll",
                               L"opengl32.dll", L"d3d9.dll" };
    int n = 0;
    for (int i = 0; i < 5; i++) {
        std::wstring p = gameDir + L"\\" + names[i];
        if (!IsReShadeFile(p)) continue;
        if (DeleteFileW(p.c_str())) {
            Log(std::wstring(L"  전에 넣어둔 리세이드를 치웠습니다: ") + names[i]);
            n++;
        } else {
            Log(std::wstring(L"  [경고] 리세이드를 치우지 못했습니다: ") + names[i]);
        }
    }
    // 2026-10-18. 효과 파일(reshade-shaders)은 지우지 않는다.
    // 레데리2에서 지웠더니 매니저로 깔린 리세이드가 이렇게 띄웠다 —
    //   구성된 이펙트 검색 경로에서 이펙트 파일(.fx)을 찾을 수 없습니다
    // 그 폴더는 어느 리세이드가 떠도 같이 쓴다. dll 만 치우면 된다.
    (void)n;
}

// 리세이드 매니저가 이 게임을 이미 들고 있는가.
//
// 2026-10-18 레데리2에서 확인. 매니저로 깐 분들은 게임 폴더가 아니라
//   C:\ProgramData\ReShade\ReShadeApps.ini
// 에 게임 실행 파일 경로가 적혀 있고, 매니저가 게임이 켜질 때 자기 리세이드를
// 끼워 넣는다. 우리가 모르고 하나 더 넣으면 두 벌이 올라오고, 나중 것이
// 스스로 죽는다. ReShade.log1 에 그대로 찍힌다 —
//   Another ReShade instance was already loaded! Aborting initialization
// 그래서 그런 게임에는 우리 리세이드를 넣지 않는다. 매니저 것을 그대로 쓰시면 된다.
static bool ReShadeManagerHasGame(const std::wstring& gameDir) {
    wchar_t pd[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring ini = std::wstring(pd) + L"\\ReShade\\ReShadeApps.ini";
    std::string raw = ReadFileBytes(ini);
    if (raw.empty()) return false;

    // 파일은 UTF-8(BOM 있을 수 있음)이다. 경로 비교는 소문자로 한다.
    std::wstring apps = LowerW(U2W(raw));
    std::wstring dir  = LowerW(gameDir);
    for (size_t i = 0; i < dir.size(); i++) if (dir[i] == L'/') dir[i] = L'\\';
    return apps.find(dir) != std::wstring::npos;
}

static bool InstallReShadeBeside(const std::wstring& gameDir, const std::wstring& optiProxy,
                                 std::vector<std::wstring>& added) {
    // 이미 리세이드가 있으면 손대지 않는다.
    // 다만 ReShade.log 가 없으면 한 번도 뜨지 못한 것이다. 리세이드는 뜰 때 그 파일을 만든다.
    // 게임마다 d3d11 인지 d3d12 인지 미리 맞히기가 어려워서, 틀렸으면 여기서 반대쪽으로 옮긴다.
    // 사용자는 "창이 안 뜨면 설치를 한 번 더" 만 알면 된다. (2026-10-09)
    {
        const wchar_t* rsHooks[] = { L"d3d11.dll", L"d3d12.dll", L"dxgi.dll", L"opengl32.dll", L"d3d9.dll" };
        std::wstring have;
        for (int i = 0; i < (int)(sizeof(rsHooks) / sizeof(rsHooks[0])); i++) {
            std::wstring p = gameDir + L"\\" + rsHooks[i];
            if (FileExists(p) && IsReShadeFile(p)) { have = rsHooks[i]; break; }
        }
        if (!have.empty()) {
            if (FileExists(gameDir + L"\\ReShade.log")) {
                Log(L"  리세이드는 이미 있고 잘 뜨고 있습니다. 그대로 둡니다.");
                // 2026-10-18. 그대로 두더라도 기록에는 적는다.
                // 안 적으면 다음 [되돌리기] 때 이 파일이 남아서,
                // 설치 전으로 돌아갔다고 해놓고 리세이드만 남는다.
                added.push_back(have);
                return true;
            }
            // 로그가 없다고 바로 틀렸다고 보면 안 된다. 아직 게임을 안 켰을 수도 있다.
            // 옵티스케일러는 게임이 돌 때마다 자기 로그를 갱신한다.
            // 그게 리세이드 dll 보다 새것일 때만 "켰는데도 안 떴다"가 성립한다.
            if (!IsNewerThan(gameDir + L"\\OptiScaler.log", gameDir + L"\\" + have)) {
                Log(L"  리세이드는 이미 있습니다. 아직 게임을 켜지 않으신 것 같아 그대로 둡니다.");
                Log(L"  게임을 켜고 8 을 눌러도 창이 안 뜨면 설치를 한 번 더 눌러 주세요.");
                added.push_back(have);
                return true;
            }
            std::wstring other;
            if (LowerW(have) == L"d3d11.dll")      other = L"d3d12.dll";
            else if (LowerW(have) == L"d3d12.dll") other = L"d3d11.dll";
            if (other.empty() || FileExists(gameDir + L"\\" + other)) {
                Log(L"  리세이드는 이미 있습니다. 그대로 둡니다.");
                added.push_back(have);
                return true;
            }
            Log(L"  리세이드가 " + have + L" 로 있는데 ReShade.log 가 없습니다. 한 번도 뜨지 못한 것입니다.");
            Log(L"  이름이 맞지 않는 것으로 보고 " + other + L" 로 옮겨 봅니다.");
            if (MoveFileW((gameDir + L"\\" + have).c_str(), (gameDir + L"\\" + other).c_str())) {
                added.push_back(other);
                Log(L"  옮겼습니다. 게임을 켜고 " + HotKeyName(HK_RS_OVERLAY) + L" 키를 눌러 보세요.");
            } else {
                wchar_t eb[200];
                swprintf_s(eb, L"  [건너뜀] 옮기지 못했습니다 (윈도우 오류 %lu)", GetLastError());
                Log(eb);
            }
            return true;
        }
    }

    std::wstring rsSrc = SelfDir() + L"\\" + DATA_DIR + L"\\" + RS_DIR;
    if (!DirExists(rsSrc)) {
        Log(L"  [건너뜀] 리세이드 재료가 프로그램 옆에 없습니다.");
        return false;
    }

    // DX11 인지 DX12 인지 가린다. 한 가지만 봐서는 안 된다.
    // 엔비디아 스트림라인을 쓰는 게임은 exe 안에 D3D dll 이름이 아예 없다.
    // sl.interposer.dll 이 대신 들고 있기 때문이다.
    // 2026-10-09 위처3(bin\x64_dx12)에서 d3d12/d3d11/dxgi 세 문자열이 모두 없었고,
    // 그래서 DX11 로 잘못 보고 리세이드를 d3d11.dll 로 넣었다.
    std::wstring exe = FindGameExe(gameDir);
    std::wstring lowdir = LowerW(gameDir);
    bool dx12 = false;
    const wchar_t* why = L"";
    if (lowdir.find(L"dx12") != std::wstring::npos || lowdir.find(L"d3d12") != std::wstring::npos) {
        dx12 = true; why = L"폴더 이름";
    }
    if (!dx12 && !exe.empty() && ExeHasString(exe, "d3d12.dll")) {
        dx12 = true; why = L"실행 파일 안의 이름";
    }
    if (!dx12) {
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW((gameDir + L"\\*d3d12*").c_str(), &fd);
        if (hf != INVALID_HANDLE_VALUE) { FindClose(hf); dx12 = true; why = L"폴더 안의 d3d12 파일"; }
    }

    std::vector<std::wstring> order;
    // ── 2026-10-18. dxgi.dll 자리가 비어 있으면 리세이드를 거기 넣는다.
    //
    // 왜 : 리세이드가 d3d12.dll 자리에 있으면 그쪽이 DXGI 팩토리를 만든다.
    // 그러면 winmm.dll 자리에 있는 DLSS 5 가 "부르는 쪽이 게임이 아니다" 라며
    // 비켜서, 화면을 한 번도 못 잡는다. Insert 창이 안 열린다.
    // 붉은사막에서 팩토리 0개 · 스왑체인 0개로 확인했다.
    //
    // 리세이드가 dxgi.dll 자리인 게임에서는 그런 일이 없었다.
    //   섀도우 툼레이더  리세이드 dxgi + DLSS 5 winmm   팩토리 14개, 창 뜸
    //   9월 위쳐3       같은 배치에서 정상이었다 (STATUS.md 1.0.0.5)
    //
    // DLSS 5 가 dxgi 를 쓰는 게임(위쳐3·라오어1)은 그대로 d3d12 로 간다.
    // 스트림라인 게임이면서 DLSS 5 가 winmm 자리인 경우에만 바꾼다.
    // 그 조합에서만 리세이드가 DXGI 를 쥐어 DLSS 5 를 막는다.
    // 나머지 게임은 지금 잘 되고 있으니 건드리지 않는다.
    // 2026-10-18. 리세이드를 dxgi 자리로 보냈다가 프래그마타가 터져서 되돌렸다.
    if (dx12) { order.push_back(L"d3d12.dll"); order.push_back(L"d3d11.dll"); }
    else      { order.push_back(L"d3d11.dll"); order.push_back(L"d3d12.dll"); }
    if (LowerW(optiProxy) == L"dxgi.dll") order.push_back(L"dxgi.dll");
    order.push_back(L"opengl32.dll");

    std::wstring name;
    for (size_t i = 0; i < order.size(); i++) {
        if (LowerW(order[i]) == LowerW(optiProxy)) continue;
        if (FileExists(gameDir + L"\\" + order[i])) continue;
        name = order[i]; break;
    }
    if (name.empty()) {
        Log(L"  [건너뜀] 리세이드를 넣을 빈 이름이 없습니다. 쓰시려면 직접 넣으셔야 합니다.");
        return false;
    }

    Status(L"리세이드를 같이 넣는 중입니다...");
    if (dx12) Log(std::wstring(L"  이 게임은 DirectX 12 로 보입니다. (") + why + L")");
    else      Log(L"  이 게임은 DirectX 11 로 보입니다.");
    if (!CopyFileW((rsSrc + L"\\" + RS_DLL).c_str(), (gameDir + L"\\" + name).c_str(), TRUE)) {
        wchar_t eb[220];
        swprintf_s(eb, L"  [건너뜀] 리세이드를 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
        Log(eb);
        return false;
    }
    added.push_back(name);
    // 2026-10-23. 자리 이름이 박혀 있어서, 옵티가 winmm 으로 들어간 게임에서도
    // "dxgi.dll 은 DLSS 5 가 쓰고 있습니다" 라고 적었다. 호라이즌FW 에서 걸렸다.
    {
        wchar_t b[260];
        swprintf_s(b, L"  리세이드를 %s 로 넣었습니다. (%s 은 DLSS 5 가 쓰고 있습니다)",
                   name.c_str(), optiProxy.c_str());
        Log(b);
    }

    std::wstring iniDst = gameDir + L"\\" + RS_INI;
    if (FileExists(iniDst)) {
        // 남의 ReShade.ini 는 그대로 둔다. 다만 창 여는 키만큼은 맞춰 넣는다.
        // 안 그러면 리세이드가 자기 기본 키(Home)로 열리는데 우리는 8 이라고
        // 안내해서, 8 을 눌러도 안 뜨고 Home 을 누르면 모르는 창이 뜬다.
        // 2026-10-15 제보. 에픽판 위쳐3, 316바이트짜리 ReShade.ini 가 있던 경우.
        //
        // 2026-10-27. 창에서 고른 값을 쓴다. 칸에는 이 파일의 지금 값이 띄워져
        // 있으므로, 대표님이 안 건드리면 같은 값을 다시 쓰는 셈이라 아무것도 안 바뀐다.
        {
            char kb[32];
            sprintf_s(kb, "%d,0,0,0", HotKeyVk(HK_RS_OVERLAY));
            SetIniKey(iniDst, "INPUT", "KeyOverlay", kb);
        }
        // 2026-10-18 제보. 게임 설정 화면을 누르면 10초쯤 멈춘다는 분이 계셨다.
        // 효과 파일을 886개 넣어드리는데 이 값이 0 이면 꺼둔 효과까지 전부 읽는다.
        // 화면을 다시 만드는 자리마다 그만큼 멈춘다. 1 이면 켜 둔 것만 읽는다.
        // PerformanceMode 는 건드리지 않는다. 그걸 켜면 창에서 값을 못 바꾼다.
        Log(L"  ReShade.ini 는 이미 있어 그대로 둡니다. 창 여는 키만 맞췄습니다: "
            + HotKeyName(HK_RS_OVERLAY));
    } else if (CopyFileW((rsSrc + L"\\" + RS_INI).c_str(), iniDst.c_str(), TRUE)) {
        added.push_back(RS_INI);
        Log(L"  ReShade.ini 를 넣었습니다. (" + HotKeyName(HK_RS_OVERLAY)
            + L" 키로 창이 열립니다. " + HotKeyName(HK_DLSS_PANEL) + L" 는 DLSS 5 창입니다)");
    }
    SetReShadeCachePath(iniDst);

    // 2026-10-24. 옵티 길에서도 보이는 효과를 켜 준다.
    // 이 길은 신경망을 옵티가 직접 돌리므로 모션벡터 공급원도 DLSS5_Feed 도
    // 넣지 않는다. 샤픈과 그레인 둘뿐이다.
    //
    // 프리셋이 비어 있을 때만 넣는다. 하나라도 켜 두셨으면 손대지 않는다.
    // 맞춰 두신 화면을 우리가 바꿔 놓으면 안 된다.
    // 되돌리기도 이래야 깨끗하다. 쓰시던 프리셋에 효과만 켜 두면 그 파일은
    // 설치 기록에 안 적혀서 되돌려도 그대로 켜져 있는다.
    {
        std::wstring preset = gameDir + L"\\" + RS_PRESET;
        bool had = FileExists(preset);
        if (!PresetIsEmpty(preset)) {
            Log(L"  ReShadePreset.ini 에 쓰시던 효과가 있어 건드리지 않습니다.");
        } else {
            // 비어 있어도 원래 있던 파일이면 백업 폴더에 넣어 둔다.
            // 그러면 되돌리기가 그 빈 프리셋으로 덮어쓴다.
            // 지우는 것도 아니고 우리 효과가 남지도 않는다. (2026-10-24)
            if (had) {
                CreateDirectoryW((gameDir + L"\\" + BACKUP_DIR).c_str(), NULL);
                std::wstring bak = gameDir + L"\\" + BACKUP_DIR + L"\\" + RS_PRESET;
                if (!FileExists(bak)) { CopyFileW(preset.c_str(), bak.c_str(), TRUE); }
            }
            if (AddLookTechniques(preset)) {
                added.push_back(RS_PRESET);
                Log(L"  보이는 효과를 켰습니다: AdaptiveSharpen, FilmGrain2");
                Log(L"     " + HotKeyName(HK_RS_OVERLAY) + L" 로 창을 열고 "
                    + HotKeyName(HK_RS_EFFECTS) + L" 로 껐다 켜면 차이가 보입니다.");
            }
        }
    }

    std::wstring shDst = gameDir + L"\\" + RS_SHADERS;
    if (HasFxFiles(shDst, 3)) {
        Log(L"  reshade-shaders 는 이미 있어 건드리지 않습니다.");
    } else if (DirExists(rsSrc + L"\\" + RS_SHADERS)) {
        Status(L"효과 파일을 넣는 중입니다...");
        std::vector<std::wstring> got;
        CopyTree(rsSrc + L"\\" + RS_SHADERS, shDst, got);
        // 효과 파일은 설치 기록에 적지 않는다. 적으면 되돌리기가 886개를 지운다.
        //
        // 2026-10-25. 캐시를 게임 폴더에 두도록 고치면서 되돌리기가 캐시를
        // 안 지우게만 했는데, 효과 파일은 그대로 지워지고 있었다. 캐시는
        // 효과 파일을 보고 구운 것이라 효과가 새 파일이 되면 쓸모가 없어진다.
        // 라오어 1에서 되돌리기 뒤 다시 깔았더니 564개를 다시 구웠다.
        // 캐시만 남기는 것은 반쪽이었다.
        //
        // 이 폴더는 어느 리세이드가 떠도 같이 쓰는 공용 폴더이기도 하다.
        // 지우면 매니저로 깐 분의 리세이드가 "이펙트 파일을 찾을 수 없습니다" 를 띄운다.
        // (2026-10-18 레데리2) 그래서 애초에 지우면 안 되는 폴더였다.
        wchar_t b[160];
        swprintf_s(b, L"  reshade-shaders 를 넣었습니다. (파일 %d개)", (int)got.size());
        Log(b);
    }
    return true;
}

// ── B 모드 준비: DLSS5-Feeder 를 넣는다.
// 게임에 DLSS 가 없으면 가로챌 NGX 호출 자체가 없다. Feeder 가 그 호출을 만들어 주고,
// 이어서 넣는 OptiScaler DLSS-NR 이 신경망 패스를 건다.
// Feeder 는 ReShade 애드온이라 ReShade 가 이미 있어야 한다. ReShade 자체는 건드리지 않는다.
static bool InstallFeeder(const std::wstring& gameDir, std::vector<std::wstring>& added) {
    std::wstring src = SelfDir() + L"\\" + DATA_DIR + L"\\" + FEED_DIR;
    if (!DirExists(src)) {
        Log(L"[실패] data 폴더가 프로그램 옆에 없습니다.");
        Log(L"       압축을 푸신 폴더에서 실행해 주세요. 지금 위치: " + SelfDir());
        return false;
    }

    const wchar_t* hooks[] = { L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"d3d9.dll", L"opengl32.dll" };
    std::wstring rs;
    for (int i = 0; i < 5; i++) {
        std::wstring p = gameDir + L"\\" + hooks[i];
        if (FileExists(p) && IsReShadeFile(p)) { rs = p; break; }
    }

    std::wstring NL(1, (wchar_t)10);
    if (rs.empty()) {
        Log(L"이 게임에는 ReShade 가 없습니다. 같이 들어 있는 것으로 넣습니다.");
        Status(L"ReShade 를 넣는 중입니다...");
        std::wstring rsSrc = SelfDir() + L"\\" + DATA_DIR + L"\\" + RS_DIR;
        if (!DirExists(rsSrc)) {
            Log(L"[실패] data 폴더가 프로그램 옆에 없습니다.");
            Log(L"       압축을 푸신 폴더에서 실행해 주세요. 지금 위치: " + SelfDir());
            return false;
        }
        std::wstring dst = gameDir + L"\\dxgi.dll";
        if (FileExists(dst)) {
            Log(L"[실패] dxgi.dll 이 이미 있는데 ReShade 가 아닙니다.");
            Log(L"       다른 프로그램이 쓰고 있어 덮어쓰면 그쪽이 망가집니다. 여기서 멈춥니다.");
            return false;
        }
        if (!CopyFileW((rsSrc + L"\\" + RS_DLL).c_str(), dst.c_str(), TRUE)) {
            wchar_t eb[200];
            swprintf_s(eb, L"[실패] ReShade 를 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
            Log(eb);
            return false;
        }
        added.push_back(L"dxgi.dll");
        rs = dst;
        Log(L"  ReShade 를 dxgi.dll 로 넣었습니다.");

        std::wstring iniDst = gameDir + L"\\" + RS_INI;
        if (FileExists(iniDst)) {
            {
                char kb[32];
                sprintf_s(kb, "%d,0,0,0", HotKeyVk(HK_RS_OVERLAY));
                SetIniKey(iniDst, "INPUT", "KeyOverlay", kb);
            }
                Log(L"  ReShade.ini 는 이미 있어 그대로 둡니다. 창 여는 키만 맞췄습니다: "
                    + HotKeyName(HK_RS_OVERLAY));
            } else if (CopyFileW((rsSrc + L"\\" + RS_INI).c_str(), iniDst.c_str(), TRUE)) {
            added.push_back(RS_INI);
            Log(L"  ReShade.ini 를 넣었습니다. (" + HotKeyName(HK_RS_OVERLAY) + L" 키로 창이 열립니다)");
        }
        SetReShadeCachePath(iniDst);

        std::wstring shDst = gameDir + L"\\" + RS_SHADERS;
        if (HasFxFiles(shDst, 3)) {
            Log(L"  reshade-shaders 는 이미 있어 건드리지 않습니다.");
        } else if (DirExists(rsSrc + L"\\" + RS_SHADERS)) {
            if (DirExists(shDst))
                Log(L"  reshade-shaders 폴더는 있지만 효과 파일(.fx)이 없어 채워 넣습니다.");
            Status(L"효과 파일을 넣는 중입니다...");
            std::vector<std::wstring> got;
            CopyTree(rsSrc + L"\\" + RS_SHADERS, shDst, got);
            // 설치 기록에 적지 않는다. 까닭은 위 InstallReShadeBeside 와 같다. (2026-10-25)
            wchar_t b[160];
            swprintf_s(b, L"  reshade-shaders 를 넣었습니다. (파일 %d개)", (int)got.size());
            Log(b);
        }
    } else {
        Log(L"설치된 ReShade 를 찾았습니다: " + rs);
        if (!IsReShadeAddonBuild(rs)) {
            Log(L"  → 애드온을 받지 않는 버전입니다. 이대로는 동작하지 않습니다.");
            std::wstring m;
            m += L"이미 설치된 ReShade 가 애드온을 지원하지 않는 버전입니다." + NL + NL;
            m += L"reshade.me 에서 [with full add-on support] 버전으로 다시 설치해 주세요." + NL;
            m += L"설정과 프리셋은 그대로 유지됩니다." + NL + NL;
            m += L"이미 설치된 ReShade 는 이 프로그램이 건드리지 않습니다.";
            MessageBoxW(g_hWnd, m.c_str(), L"ReShade 버전을 바꿔 주세요",
                        MB_OK | MB_ICONWARNING | MB_TOPMOST);
            ShellExecuteW(NULL, L"open", L"https://reshade.me", NULL, NULL, SW_SHOWNORMAL);
            return false;
        }
        Log(L"  → 애드온을 받아 주는 버전입니다. 그대로 두고 애드온만 넣습니다.");
    }

    std::wstring adDst = gameDir + L"\\" + FEED_ADDON;
    if (!CopyFileW((src + L"\\" + FEED_ADDON).c_str(), adDst.c_str(), FALSE)) {
        wchar_t eb[200];
        swprintf_s(eb, L"[실패] Feeder 애드온을 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
        Log(eb);
        return false;
    }
    added.push_back(FEED_ADDON);
    Log(L"DLSS5-Feeder 애드온을 넣었습니다.");

    // 설정 파일. 이미 있으면 손대지 않는다 (직접 맞춰 두신 값을 지우면 안 된다).
    // 기본값은 4K 에서도 쓸 수 있게 처리 해상도를 67% 로 낮춰 둔다.
    // 100% 로 두면 4K 에서 프레임당 1초가 걸려 사실상 멈춘 것처럼 보인다.
    std::wstring cfgDst = gameDir + L"\\" + FEED_CFG;
    if (FileExists(cfgDst)) {
        Log(L"  설정 파일은 이미 있어 건드리지 않습니다.");
    } else if (CopyFileW((src + L"\\" + FEED_CFG).c_str(), cfgDst.c_str(), TRUE)) {
        added.push_back(FEED_CFG);

        // 화면 해상도에 맞춰 처리 해상도를 정한다.
        int sw = 0, sh = 0;
        int pct = PickWorkResolution(sw, sh);
        std::string cfg = ReadFileBytes(cfgDst);

        // 값 하나를 제자리에서 갈아 끼운다.
        struct Setter {
            static void Put(std::string& t, const char* key, int val) {
                char line[64];
                sprintf_s(line, "%s%d", key, val);
                size_t k = t.find(key);
                if (k == std::string::npos) return;
                size_t e = t.find((char)10, k);
                if (e == std::string::npos) e = t.size();
                size_t e2 = e;
                while (e2 > k && t[e2-1] == (char)13) e2--;
                t = t.substr(0, k) + line + t.substr(e2);
            }
        };

        Setter::Put(cfg, "work_resolution=", pct);

        // 처리 해상도가 100% 면 되돌릴 것이 없으므로 FSR1 을 끈다.
        // 켜 두면 필요 없는 리샘플이 한 번 더 걸려 화면이 흐려진다.
        Setter::Put(cfg, "work_upscale=", (pct >= 100) ? 0 : 1);

        WriteFileBytes(cfgDst, cfg);
        wchar_t b[220];
        swprintf_s(b, L"  설정 파일을 넣었습니다. 화면이 %dx%d 이라 처리 해상도를 %d%% 로 정했습니다.",
                   sw, sh, pct);
        Log(b);
        if (pct == 100)
            Log(L"     화면이 크지 않아 화질 손해 없이 그대로 돌립니다. (FSR1 되돌림은 꺼 둡니다)");
        else
            Log(L"     4K 급 화면이라 낮춰 잡았습니다. 프레임이 남으면 dlss5-feed.cfg 에서 올리셔도 됩니다.");
    }

    std::wstring hostDir = gameDir + L"\\host64";
    CreateDirectoryW(hostDir.c_str(), NULL);
    if (CopyFileW((src + L"\\host64\\" + FEED_HOST).c_str(),
                  (hostDir + L"\\" + FEED_HOST).c_str(), FALSE)) {
        added.push_back(std::wstring(L"host64\\") + FEED_HOST);
        Log(L"  보조 실행 파일을 넣었습니다.");
    }


    std::wstring shDir = gameDir + L"\\reshade-shaders\\Shaders";
    CreateDirectoryW((gameDir + L"\\reshade-shaders").c_str(), NULL);
    CreateDirectoryW(shDir.c_str(), NULL);
    if (CopyFileW((src + L"\\shaders\\" + FEED_FX).c_str(),
                  (shDir + L"\\" + FEED_FX).c_str(), FALSE)) {
        added.push_back(std::wstring(L"reshade-shaders\\Shaders\\") + FEED_FX);
        Log(L"  셰이더를 넣었습니다: " + std::wstring(FEED_FX));
    }
    // 프리셋에서 효과를 켜 준다. 이게 없으면 모션 벡터가 0 으로 들어가 신경망이 제대로 못 돈다.
    {
        std::wstring preset = gameDir + L"\\" + RS_PRESET;
        bool existed = FileExists(preset);
        // 보이는 효과를 넣을지는 손대기 전에 정해 둔다.
        // 바로 아래 EnableFeedTechniques 가 우리 둘을 켜 넣으므로,
        // 그 뒤에 보면 빈 프리셋도 비어 있지 않은 것으로 보인다. (2026-10-24)
        bool wasEmpty = PresetIsEmpty(preset);
        // 비어 있어도 원래 있던 파일이면 백업 폴더에 넣어 둔다.
        // 되돌리기가 그 빈 프리셋으로 덮어쓴다. (2026-10-24)
        if (existed && wasEmpty) {
            CreateDirectoryW((gameDir + L"\\" + BACKUP_DIR).c_str(), NULL);
            std::wstring bak = gameDir + L"\\" + BACKUP_DIR + L"\\" + RS_PRESET;
            if (!FileExists(bak)) { CopyFileW(preset.c_str(), bak.c_str(), TRUE); }
        }
        if (EnableFeedTechniques(preset)) {
            if (!existed || wasEmpty) { added.push_back(RS_PRESET); }
            Log(L"  프리셋에서 모션 벡터와 DLSS5_Feed 효과를 켰습니다.");
            if (existed) Log(L"     쓰시던 효과는 그대로 두고 앞에 두 개만 추가했습니다.");
            if (!wasEmpty) {
                Log(L"  ReShadePreset.ini 에 쓰시던 효과가 있어 보이는 효과는 건드리지 않습니다.");
            } else if (AddLookTechniques(preset)) {
                Log(L"  보이는 효과 두 개도 켰습니다: AdaptiveSharpen, FilmGrain2");
            }
        } else {
            Log(L"  [경고] 프리셋을 쓰지 못했습니다. 게임에서 직접 켜셔야 합니다.");
            Log(L"         리세이드 창에서 DH_UBER_MOTION_020 과 DLSS5_Feed 를 켜시고,");
            Log(L"         DH_UBER_MOTION_020 이 DLSS5_Feed 보다 위에 오게 하세요.");
        }
    }

    if (!HasFxFiles(gameDir + L"\\reshade-shaders", 3)) {
        Log(L"  [주의] 효과 파일(.fx)이 이것 하나뿐입니다. ReShade 설치가 온전하지 않을 수 있습니다.");
    }

    // 신경망 소비자: RenoDX DLSS5 애드온.
    // OptiScaler 는 이 조합에서 D3D12 브리지가 둘이 되어 힙이 깨진다. 여기서는 쓰지 않는다.
    std::wstring rdDst = gameDir + L"\\" + RENODX_ADDON;
    if (!CopyFileW((src + L"\\" + RENODX_ADDON).c_str(), rdDst.c_str(), FALSE)) {
        wchar_t eb[200];
        swprintf_s(eb, L"[실패] RenoDX 애드온을 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
        Log(eb);
        return false;
    }
    added.push_back(RENODX_ADDON);
    Log(L"RenoDX 신경망 애드온을 넣었습니다.");

    // DLSS 런타임. Feeder 는 DLSS 호출을 만들어 내는 것이라 이 파일이 있어야 한다.
    // 이것이 없으면 SuperSampling.Available=0 이 되고 세션이 열리지 않는다.
    // 게임이 이미 갖고 있으면 그것을 그대로 둔다. 버전을 낮추면 안 된다.
    {
        std::wstring dst = gameDir + L"\\" + DLSS_DLL;
        std::wstring srcDll = src + L"\\" + DLSS_DLL;
        if (FileExists(dst)) {
            Log(L"  DLSS 런타임은 이미 있어 건드리지 않습니다.");
        } else if (!FileExists(srcDll)) {
            Log(L"  [경고] 같이 들어 있어야 할 nvngx_dlss.dll 이 없습니다.");
        } else {
            Status(L"DLSS 런타임을 넣는 중입니다...");
            std::wstring tmpDst = dst + L".part";
            DeleteFileW(tmpDst.c_str());
            long long want = FileSizeOf(srcDll);
            bool ok = CopyFileW(srcDll.c_str(), tmpDst.c_str(), FALSE) != 0;
            if (ok && want > 0 && FileSizeOf(tmpDst) != want) ok = false;
            if (ok) ok = MoveFileW(tmpDst.c_str(), dst.c_str()) != 0;
            if (ok) {
                added.push_back(DLSS_DLL);
                Log(L"  DLSS 런타임(nvngx_dlss.dll)을 넣었습니다.");
            } else {
                DeleteFileW(tmpDst.c_str());
                wchar_t eb[200];
                swprintf_s(eb, L"[실패] DLSS 런타임을 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
                Log(eb);
                return false;
            }
        }
    }

    // 켜고 끄는 키를 F7 로 맞춘다. OptiScaler 방식과 같은 키라 사용자가 하나만 외우면 된다.
    // 이미 값이 있으면 직접 바꾸신 것이므로 손대지 않는다.
    {
        std::wstring rsIni = gameDir + L"\\" + RS_INI;
        std::string cur = ReadFileBytes(rsIni);
        if (cur.find("NRToggleKey=") == std::string::npos) {
            if (SetIniKey(rsIni, "RenoDX.DLSS5", "NRToggleKey", "118"))
                Log(L"  껐다 켜는 키를 맞췄습니다: " + HotKeyName(HK_DLSS_TOGGLE));
        } else {
            Log(L"  껐다 켜는 키는 이미 정해져 있어 그대로 둡니다.");
        }

        // 2026-10-22. 리세이드 길도 옵티 길과 같은 기본값으로 맞춘다.
        //
        // 1.0.2.1 까지는 켜고 끄는 키만 맞추고 화질 값은 애드온 순정으로 두었다.
        // 순정은 세기가 1.0 이라 켜도 티가 거의 안 난다. 설치하고도 뭐가 달라졌는지
        // 모르겠다는 소리가 거기서 나왔다.
        //
        // 아래 값은 대표님 데이즈곤에서 쓰고 계신 설정을 그대로 옮긴 것이다.
        // 2026-09-25 같은 자리에서 한 값씩 바꿔 찍어 고른 값이고,
        // NRStyle 1(natural) 은 옵티 길의 Style 1 과 같은 선택이다.
        //
        // 주의 - 이 값들은 덮어쓴다. 애드온은 게임을 끌 때 ini 에 모든 키를 쓰므로
        // 한 번이라도 켜 본 분은 키가 전부 들어 있다. "없을 때만 쓰기"로 하면
        // 그분들에게는 새 기본값이 영영 안 간다. 창에서 바로 되돌릴 수 있는 값들이다.
        struct RdSet { const char* key; const char* val; };
        static const RdSet RD[] = {
            { "NRIntensity",      "2" },   // 이마.볼 +50.4%, 프레임 변화 없음
            { "NRLocalStructure", "2" },   // 수염 +57.9%. 이 길에서 가장 큰 폭
            { "NRSkinStructure",  "1" },
            { "NRLocalTone",      "0" },   // 올리면 납작해진다 (2.0 에서 얼굴 -12.8%)
            { "NRGlobalTone",     "1" },
            { "NRColorStrength",  "1" },
            { "NRUpscaledPasses", "1" },   // 기본 1패스. 창에서 올릴 수 있다
            { "NRRenderPasses",   "0" },
            { "NRPresentPasses",  "0" },
            { "EnableHooks",      "2" },
        };
        for (int ri = 0; ri < (int)(sizeof(RD) / sizeof(RD[0])); ri++) {
            SetIniKey(rsIni, "RenoDX.DLSS5", RD[ri].key, RD[ri].val);
        }
        Log(L"  신경망 기본값을 맞췄습니다 (Natural, 1패스).");

        // NRStyle 은 따로 다룬다. 2 로 되어 있으면 다음 실행 때 게임이 안 켜진다.
        // 제작자 문서에 적힌 알려진 문제다. 1(natural) 로 맞추되 2 는 반드시 치운다.
        std::string cur2 = ReadFileBytes(rsIni);
        if (cur2.find("NRStyle=2") != std::string::npos) {
            if (SetIniKey(rsIni, "RenoDX.DLSS5", "NRStyle", "1"))
                Log(L"  NR Style 이 2 로 되어 있어 1 로 되돌렸습니다. (2 는 다음 실행 때 게임이 안 켜집니다)");
        } else {
            SetIniKey(rsIni, "RenoDX.DLSS5", "NRStyle", "1");
        }
    }

    Log(L"Feeder 가 DLSS 호출을 만들고 RenoDX 가 신경망 패스를 겁니다.");
    return true;
}

static DWORD WINAPI WorkThread(LPVOID param) {
    Job* job = (Job*)param;
    Log(L"실행 파일이 있는 폴더를 찾고 있습니다...");
    std::wstring gameDir = ResolveExeDir(job->dir);

    // 실행 파일이 하위 폴더에 있는 게임이 있다. (위처3: bin\\x64_dx12)
    // 파일은 실행 파일이 있는 폴더에 넣어야 게임이 읽는다.
    {
        std::wstring exe = FindGameExe(gameDir);
        if (!exe.empty()) {
            size_t sl = exe.find_last_of(L"\\\\");
            if (sl != std::wstring::npos) {
                std::wstring d = exe.substr(0, sl);
                if (LowerW(d) != LowerW(gameDir)) {
                    Log(L"  게임 본체가 하위 폴더에 있습니다: " + exe);
                    gameDir = d;
                }
            }
        }
    }

    std::wstring backup = gameDir + L"\\" + BACKUP_DIR;
    Log(L"  → " + gameDir);
    {
        wchar_t b[200];
        swprintf_s(b, L"  이 드라이브 여유 공간: %lld MB", FreeSpaceMB(gameDir));
        Log(b);
        Log(L"  이미 들어 있는 파일 (충돌 확인용):");
        const wchar_t* watch[] = { L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll",
                                   L"wininet.dll", L"winhttp.dll", L"nvngx.dll", L"nvngx_dlss.dll",
                                   L"nvngx_dlssnr.dll", L"nvngx.dll_dlssnr.dll", L"OptiScaler.ini",
                                   L"ReShade.ini", L"renodx-dlss5.addon64", L"dlss5-feed.addon64" };
        bool any = false;
        for (int i = 0; i < 14; i++) {
            std::wstring p = gameDir + L"\\" + watch[i];
            if (!FileExists(p)) continue;
            any = true;
            long long sz = FileSizeOf(p);
            wchar_t line[300];
            swprintf_s(line, L"     %-24s %lld 바이트", watch[i], sz);
            Log(line);
        }
        if (!any) Log(L"     (없음 - 깨끗한 상태입니다)");
    }

    if (job->restore) {
        Log(L"───── 되돌리기를 시작합니다");

        // 예전 판이 엉뚱한 하위 폴더에 설치했을 수 있다. (배너로드: Watchdog 폴더)
        // 고른 폴더에 설치 기록이 없으면 게임 폴더 아래에서 찾아 그 폴더를 되돌린다.
        if (!FileExists(gameDir + L"\\" + MANIFEST)) {
            std::wstring found;
            if (FindFileDeep(ResolveExeDir(job->dir), MANIFEST, 4, found)) {
                size_t s2 = found.find_last_of(L"\\");
                if (s2 != std::wstring::npos) {
                    gameDir = found.substr(0, s2);
                    backup = gameDir + L"\\" + BACKUP_DIR;
                    Log(L"  설치 기록이 다른 폴더에 있어 그 폴더를 되돌립니다: " + gameDir);
                }
            }
        }

        // 게임이 켜져 있으면 파일이 잠겨 되돌릴 수 없다. 먼저 확인한다.
        {
            std::wstring which;
            if (AnyProcessRunningIn(gameDir, which)) {
                Log(L"[실패] 이 게임이 실행 중입니다: " + which);
                Log(L"       게임을 끄고 다시 눌러 주세요. 켜져 있으면 파일이 잠겨 지울 수 없습니다.");
                std::wstring NLr(1, (wchar_t)10);
                std::wstring mr;
                mr += L"게임이 아직 실행 중입니다." + NLr + NLr;
                mr += which + NLr + NLr;
                mr += L"게임을 완전히 끄신 뒤 다시 [되돌리기] 를 눌러 주세요.";
                MessageBoxW(g_hWnd, mr.c_str(), L"게임을 먼저 꺼 주세요",
                            MB_OK | MB_ICONWARNING | MB_TOPMOST);
                Status(L"게임이 실행 중입니다");
                delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
            }
        }

        std::string man = ReadFileBytes(gameDir + L"\\" + MANIFEST);
        if (man.empty()) {
            Log(L"설치 기록이 없습니다. 이 게임에는 이 프로그램으로 설치한 적이 없습니다.");
        } else {
            int n = 0, fail = 0;
            std::wstring w = U2W(man);
            size_t p = 0;
            while (p < w.size()) {
                size_t e = w.find(L'\n', p);
                std::wstring rel = w.substr(p, (e == std::wstring::npos ? w.size() : e) - p);
                while (!rel.empty() && (rel.back() == L'\r' || rel.back() == L' ')) rel.pop_back();
                if (rel.rfind(L"MOVED ", 0) == 0) {
                    std::wstring name = rel.substr(6);
                    std::wstring bak2 = backup + L"\\" + name;
                    std::wstring tgt2 = gameDir + L"\\" + name;
                    if (FileExists(bak2)) {
                        DeleteFileW(tgt2.c_str());
                        if (MoveFileW(bak2.c_str(), tgt2.c_str())) {
                            Log(L"치워 뒀던 파일을 제자리로 돌려놓았습니다: " + name);
                            n++;
                        }
                    }
                } else if (!rel.empty()) {
                    std::wstring target = gameDir + L"\\" + rel;
                    std::wstring bak = backup + L"\\" + rel;
                    // 매니페스트에는 폴더도 들어간다 (reshade-shaders 처럼).
                    // FileExists 는 폴더에 false 를 돌려주므로 따로 처리해야 한다.
                    if (DirExists(target) && !DirExists(bak)) {
                        RemoveTree(target);
                        Log(L"넣었던 폴더를 지웠습니다: " + rel);
                        n++;
                        if (e == std::wstring::npos) break;
                        p = e + 1;
                        continue;
                    }
                    if (FileExists(bak)) {
                        if (CopyFileW(bak.c_str(), target.c_str(), FALSE)) {
                            Log(L"원래 파일로 되돌렸습니다: " + rel);
                            n++;
                        } else {
                            wchar_t eb[200];
                            swprintf_s(eb, L"[경고] 되돌리지 못했습니다 (윈도우 오류 %lu): ", GetLastError());
                            Log(std::wstring(eb) + rel);
                        }
                    }
                    else if (FileExists(target) && IsSettingsFile(rel)) {
                        // 설정 파일은 게임 안에서 사용자가 바꾼 값이 저장된다. 지우면 그 설정이 사라진다.
                        // dll 이 없으면 게임은 이 파일을 읽지 않으므로 남겨 둬도 해가 없다.
                        Log(L"설정 파일은 지우지 않고 남겨 둡니다: " + rel);
                    }
                    else if (FileExists(target)) {
                        if (DeleteFileChecked(target)) { n++; }
                        else {
                            wchar_t eb[220];
                            swprintf_s(eb, L"[실패] 지우지 못했습니다 (윈도우 오류 %lu): ", GetLastError());
                            Log(std::wstring(eb) + rel);
                            fail++;
                        }
                    }
                }
                if (e == std::wstring::npos) break;
                p = e + 1;
            }
            DeleteFileW((gameDir + L"\\" + MANIFEST).c_str());
            // 설치가 넣은 폴더를 지우고, 백업해 둔 원래 폴더가 있으면 되살린다.
            // 순서가 중요하다. 백업 폴더를 먼저 지우면 되살릴 것이 없어진다.
            {
                const wchar_t* dirs[] = { L"OptiScaler", L"Licenses" };
                for (int i = 0; i < 2; i++) {
                    std::wstring tgt = gameDir + L"\\" + dirs[i];
                    std::wstring bk  = backup  + L"\\" + dirs[i];
                    RemoveTree(tgt);
                    if (!DirExists(bk)) continue;
                    std::vector<std::wstring> got;
                    CopyTree(bk, tgt, got);
                    wchar_t b[160];
                    swprintf_s(b, L"원래 폴더로 되돌렸습니다: %s (파일 %d개)", dirs[i], (int)got.size());
                    Log(b);
                    n += (int)got.size();
                }
            }
            // OptiScaler.ini 는 절대 여기서 지우지 않는다. 위 매니페스트 루프가
            // 백업본이 있으면 되살리고 없으면 지운다. 여기서 또 지우면 방금 되살린
            // 사용자의 원래 설정이 사라진다.
            //
            // 2026-10-25. 이 다섯 이름을 조건 없이 지우고 있었다. 게임이 같은 이름을
            // 가지고 있으면, 위에서 백업에서 되살려 놓은 것을 여기서 다시 지웠다.
            // 백업에 그 이름이 있으면 게임이 원래 가지고 있던 것이므로 건드리지 않는다.
            // 백업 폴더를 지우기 전에 해야 알 수 있어서 RemoveTree 보다 앞으로 옮겼다.
            const wchar_t* junk[] = { L"OptiScaler.log", L"setup_windows.bat",
                                      L"setup_linux.sh", L"READ ME - DLSS Neural Rendering.txt",
                                      L"!! EXTRACT ALL FILES TO GAME FOLDER !!" };
            for (int k = 0; k < 5; k++) {
                if (FileExists(backup + L"\\" + junk[k])) {
                    Log(std::wstring(L"게임이 원래 가지고 있던 파일이라 두었습니다: ") + junk[k]);
                    continue;
                }
                DeleteFileW((gameDir + L"\\" + junk[k]).c_str());
            }
            // 안의 파일만 지우면 빈 폴더가 남는다. 비어 있을 때만 지워진다.
            RemoveDirectoryW((gameDir + L"\\host64").c_str());
            RemoveTree(backup);
            Log(L"남은 파일까지 정리했습니다. 게임 폴더가 설치 전 상태로 돌아갔습니다.");
            wchar_t b[200];
            if (fail > 0) swprintf_s(b, L"파일 %d개를 되돌렸고, %d개는 지우지 못했습니다.", n, fail);
            else          swprintf_s(b, L"파일 %d개를 원래대로 되돌렸습니다.", n);
            Log(b);
        }
        // 말로 "됐습니다" 하지 않고, 정말 사라졌는지 직접 확인한다.
        Status(L"되돌리기가 제대로 됐는지 확인하는 중입니다...");
        {
            std::wstring left;
            int rem = CheckRevertLeftovers(gameDir, left);
            Log(L"");
            if (rem == 0) {
                Log(L"───── 확인: 넣었던 것이 모두 사라졌습니다");
            } else {
                Log(L"───── 확인: 아직 남아 있는 것이 있습니다");
                Log(left);
                std::wstring NLr(1, (wchar_t)10);
                std::wstring mr;
                // 2026-10-18. 지울 수 있는 것은 이미 다 지웠다. 여기까지 온 것은
                // 파일이 잠긴 경우뿐이다. 손으로 지우라고 시키지 않는다.
                mr += L"이 파일이 잠겨 있어서 지우지 못했습니다." + NLr + NLr;
                mr += left + NLr;
                mr += L"게임이나 스팀이 아직 켜져 있는 것 같습니다." + NLr;
                mr += L"완전히 끄신 뒤 [되돌리기] 를 한 번 더 눌러 주세요." + NLr + NLr;
                mr += L"그러면 프로그램이 알아서 치웁니다.";
                MessageBoxW(g_hWnd, mr.c_str(), L"되돌리기를 확인해 주세요",
                            MB_OK | MB_ICONWARNING | MB_TOPMOST);
            }
        }

        Log(L"───── 되돌리기를 마쳤습니다");
        Status(L"되돌리기 완료");
        delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
    }

    Log(L"───── 설치를 시작합니다");

    // ── 어느 방식으로 설치할지 정한다
    bool modeB = false, modeBVulkan = false;
    {
        std::wstring exe = FindGameExe(gameDir);
        std::wstring NLs(1, (wchar_t)10);
        Status(L"이 게임이 DLSS 를 지원하는지 확인하는 중입니다...");
        Log(L"이 게임이 DLSS 를 지원하는지 확인하고 있습니다.");
        Log(L"  실행 파일: " + (exe.empty() ? std::wstring(L"(못 찾음)") : exe));

        // 게임 실행 파일을 못 찾으면 엉뚱한 곳에 넣게 된다. 아무것도 넣지 않고 멈춘다.
        if (exe.empty()) {
            Log(L"[중단] 이 폴더에서 게임 실행 파일을 찾지 못했습니다. 아무것도 넣지 않았습니다.");
            Log(L"       [진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요.");
            std::wstring m;
            m += L"이 게임의 실행 파일을 찾지 못했습니다." + NLs + NLs;
            m += L"잘못 설치되지 않도록 아무것도 넣지 않았습니다." + NLs + NLs;
            m += L"2010년 무렵 이전 게임이라면 DirectX 9 라서" + NLs;
            m += L"DLSS 5 자체가 안 되는 게임일 수 있습니다." + NLs + NLs;
            m += L"[진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요.";
            MessageBoxW(g_hWnd, m.c_str(), L"설치하지 않았습니다", MB_OK | MB_ICONWARNING | MB_TOPMOST);
            Status(L"설치하지 않았습니다");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }

        // DX9 게임은 DLSS 5 를 걸 수 없다. 넣어 봐야 켜지지 않고 "설치 완료" 만 뜬다.
        if (IsDx9Only(exe)) {
            Log(L"[중단] 이 게임은 DirectX 9 게임입니다. 아무것도 넣지 않았습니다.");
            Log(L"       DLSS 5 는 DirectX 11 / 12, Vulkan 게임에서만 켜집니다.");
            std::wstring m;
            m += L"이 게임은 DirectX 9 로 만들어진 오래된 게임입니다." + NLs + NLs;
            m += L"DLSS 5 는 DirectX 11 / 12, Vulkan 게임에서만 켜지므로" + NLs;
            m += L"아무것도 넣지 않았습니다.";
            MessageBoxW(g_hWnd, m.c_str(), L"지원하지 않는 게임입니다", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            Status(L"지원하지 않는 게임입니다");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }

        // ── 32비트 게임은 넣지 않는다. 2026-10-24 톰레이더(2013)로 하루를 썼다.
        //
        // 먼저 조용히 실패하고 있었다. 우리는 ReShade64.dll 만 넣는데 64비트 dll 은
        // 32비트 프로세스에 아예 안 들어간다. ReShade.log 조차 안 생기는데
        // 기록에는 "설치 완료" 가 찍혔다. 자리를 dxgi 에서 d3d11 로 바꿔 가며
        // 두 번 헛짓했다. 자리 문제가 아니었다.
        //
        // 그래서 32비트 길을 만들어 봤다. 실제로 됐다 —
        //   ReShade32.dll + dlss5-feed.addon32 + host64\(dxgi.dll, renodx, nvngx 둘)
        //   1872x1052 로 5,331프레임, NR 4.6ms/frame, 83fps -> 60fps
        //
        // 그런데 **게임이 최소화된다.** 64비트 보조 프로그램이 창을 만들어야 하고,
        // 그 창이 전체 화면 게임을 밀어낸다. host_window=0 으로 숨겨도 그렇다.
        // 받으신 분은 "깔았더니 게임이 꺼진다" 고 받아들인다. 실제로 그렇게 보인다.
        // 애드온 쪽 구조라 우리가 손댈 자리가 없다.
        //
        // 그래서 DX9 게임과 같이 처리한다. 조용히 실패하지 않고 분명히 말한다.
        // 되살리려면 ExeIs32Bit 로 가른 뒤 위의 구성을 넣으면 된다.
        // ReShade32.dll 과 dlss5-feed.addon32 는 동봉에서 뺐으므로 다시 넣어야 한다.
        // (ReShade32.dll 은 공식 설치 프로그램이 32비트 게임에 넣는 그 파일이다)
        if (ExeIs32Bit(exe)) {
            Log(L"[중단] 이 게임은 32비트입니다. 아무것도 넣지 않았습니다.");
            Log(L"       DLSS 5 는 64비트 게임에서만 켜집니다.");
            std::wstring m;
            m += L"이 게임은 32비트로 만들어졌습니다." + NLs + NLs;
            m += L"DLSS 5 는 64비트 게임에서만 켜지므로" + NLs;
            m += L"아무것도 넣지 않았습니다.";
            MessageBoxW(g_hWnd, m.c_str(), L"지원하지 않는 게임입니다", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            Status(L"지원하지 않는 게임입니다");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }

        int sup = GameSupportsDlssFor(exe);
        if (sup == 1) {
            Log(L"  → 게임에 DLSS 연결이 들어 있습니다. OptiScaler 방식으로 설치합니다.");
        } else if (sup == 0) {
            modeB = true;
            Log(L"  → 이 게임에는 DLSS 가 없습니다. ReShade + RenoDX 방식으로 설치합니다.");
        } else {
            Log(L"  → 실행 파일을 읽지 못해 판단할 수 없습니다. 기본값인 OptiScaler 방식으로 갑니다.");
            Log(L"     아무 변화가 없으면 [진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요.");
        }

        LRESULT as2 = 0;
        std::wstring gl = LowerW(gameDir);
        modeBVulkan = (as2 == 2) || (as2 == 0 &&
            (gl.find(L"red dead redemption 2") != std::wstring::npos
          || gl.find(L"doom") != std::wstring::npos
          || gl.find(L"wolfenstein") != std::wstring::npos));
    }

    // REFramework 이 들어 있으면 잠시 꺼 둔다. 같이 올라오면 게임 시작할 때 꺼진다.
    // 지우지 않고 이름만 바꾼다. 되돌리기가 원래 이름으로 돌려놓는다.
    {
        std::wstring di  = gameDir + L"\\dinput8.dll";
        std::wstring off = gameDir + L"\\dinput8.dll.off";
        if (FileExists(di) && IsREFramework(di)) {
            if (FileExists(off)) { DeleteFileW(off.c_str()); }
            if (MoveFileW(di.c_str(), off.c_str())) {
                Log(L"REFramework 이 들어 있어 잠시 꺼 두었습니다. (dinput8.dll → dinput8.dll.off)");
                Log(L"  둘을 같이 올리면 게임을 시작하는 순간 꺼집니다. 제작자 쪽에도 같은 제보가 많습니다.");
                Log(L"  [되돌리기] 를 누르면 원래 이름으로 돌려놓습니다.");
            } else {
                Log(L"[경고] REFramework(dinput8.dll)을 꺼 두지 못했습니다. 게임을 끄고 다시 눌러 주세요.");
                Log(L"  이대로 두면 게임을 시작할 때 꺼질 수 있습니다.");
            }
        }
    }

    Log(L"다른 방식의 DLSS 5가 이미 걸려 있는지 확인하고 있습니다... (리세이드는 그대로 둡니다)");
    std::vector<std::wstring> conflicts = modeB ? std::vector<std::wstring>() : CollectConflicts(gameDir);
    std::vector<std::wstring> movedAside;
    if (conflicts.empty()) {
        Log(L"  → 깨끗합니다. 그대로 진행합니다.");
    } else {
        CreateDirectoryW(backup.c_str(), NULL);
        for (size_t i = 0; i < conflicts.size(); i++) {
            std::wstring from = gameDir + L"\\" + conflicts[i];
            std::wstring to = backup + L"\\" + conflicts[i];
            DeleteFileW(to.c_str());
            if (MoveFileW(from.c_str(), to.c_str())) {
                movedAside.push_back(conflicts[i]);
                Log(L"  DLSS 5가 두 번 걸리지 않도록 이 파일을 백업 폴더로 옮겼습니다: " + conflicts[i]);
            } else {
                Log(L"  [실패] " + conflicts[i] + L" 을(를) 옮기지 못했습니다. 게임이 실행 중인지 확인해 주세요.");
                Status(L"파일을 옮기지 못했습니다");
                delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
            }
        }
        Log(L"  치운 파일은 백업 폴더에 그대로 있습니다. [되돌리기]를 누르면 전부 제자리로 돌아옵니다.");
    }

    Log(L"이 폴더에 파일을 쓸 수 있는지 확인하고 있습니다...");
    std::wstring testFile = gameDir + L"\\_dlss5_write_test.tmp";
    if (!WriteFileBytes(testFile, "test")) {
        Log(L"[실패] 이 폴더에 파일을 쓸 수 없습니다. 권한이 막혀 있습니다.");
        Log(L"       이 프로그램을 마우스 오른쪽 클릭 → [관리자 권한으로 실행] 으로 다시 해 보세요.");
        Status(L"권한이 부족합니다");
        delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
    }
    DeleteFileW(testFile.c_str());
    Log(L"  → 쓸 수 있습니다.");

    std::vector<std::wstring> added;

    // DLSS 가 없는 게임은 Feeder 가 NGX 호출을 만들고 RenoDX 애드온이 신경망 패스를 건다.
    // 이 경로에서는 OptiScaler 를 넣지 않는다. D3D12 브리지가 둘이 되어 힙이 깨진다.
    if (modeB) {
        // 2026-10-23. 이 길은 리세이드가 본체다. 애드온이 리세이드 위에서 돌기 때문에
        // 빼면 아무것도 동작하지 않는다. 그래서 창에서 끄셨어도 넣는다.
        // 다만 말없이 넣지는 않는다. 끈 것을 무시했으면 그렇게 적어야 한다.
        if (SendMessageW(g_hRs, BM_GETCHECK, 0, 0) != BST_CHECKED) {
            Log(L"리세이드를 끄셨지만 이 게임은 리세이드 위에서만 동작합니다. 그래서 같이 넣습니다.");
            Log(L"  이 게임에는 DLSS 가 없어 리세이드 애드온으로 신경망을 겁니다.");
        }
        Status(L"DLSS5-Feeder 를 넣는 중입니다...");
        if (!InstallFeeder(gameDir, added)) {
            Log(L"───── 설치를 마치지 못했습니다");
            Status(L"설치 실패");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }
        if (modeBVulkan) {
            std::wstring NLv(1, (wchar_t)10);
            std::wstring mv;
            mv += L"이 게임은 Vulkan 입니다." + NLv + NLv;
            mv += L"엔비디아 Smooth Motion 을 꺼 주셔야 합니다." + NLv;
            mv += L"켜져 있으면 화면의 절반만 처리되어 심하게 깜빡이는 것처럼 보입니다." + NLv + NLv;
            mv += L"엔비디아 앱 또는 Profile Inspector 에서 이 게임의" + NLv;
            mv += L"Smooth Motion 을 꺼 주세요. DirectX 게임은 켜 두셔도 됩니다.";
            MessageBoxW(g_hWnd, mv.c_str(), L"Smooth Motion 을 꺼 주세요",
                        MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
        if (modeBVulkan) {
            Log(L"");
            Log(L"  [주의] Vulkan 게임입니다. 엔비디아 Smooth Motion 을 꺼 주세요.");
            Log(L"         켜져 있으면 절반의 화면만 처리되어 깜빡이는 것처럼 보입니다.");
            Log(L"         DirectX 게임은 켜 두셔도 됩니다.");
        }
    } else {
    std::wstring tmp = TempDir();
    std::wstring zip = tmp + L"optiscaler.zip";
    std::wstring ex  = tmp + L"extract";
    // 1순위: 프로그램 옆에 같이 넣어 배포한 zip (인터넷 없이도 설치된다)
    std::wstring sideZip = SelfDir() + L"\\" + DATA_DIR + L"\\" + OPTI_ZIP;
    if (FileExists(sideZip) && FileSizeOf(sideZip) > 50LL * 1024 * 1024) {
        Log(L"OptiScaler 가 프로그램과 같은 폴더에 들어 있습니다. 내려받지 않습니다.");
        Log(L"  위치: " + sideZip);
        zip = sideZip;
    } else if (FileExists(zip) && FileSizeOf(zip) > 50LL * 1024 * 1024) {
        Log(L"전에 받아 둔 파일이 있어 다시 받지 않습니다.");
        Log(L"  위치: " + zip);
    } else {
        if (FileExists(zip)) {
            Log(L"전에 받다 만 파일이 있어 지우고 다시 받습니다.");
            DeleteFileW(zip.c_str());
        }
        Log(L"OptiScaler " + std::wstring(OPTI_VER) + L" 를 내려받고 있습니다. 약 145MB, 인터넷 속도에 따라 몇 분 걸립니다.");
        // 박아 둔 주소는 새 판이 나오면 죽는다. 먼저 최신 주소를 물어본다.
        Status(L"최신 판이 있는지 확인하는 중입니다...");
        std::wstring useUrl = FetchLatestOptiUrl();
        if (!useUrl.empty() && useUrl != OPTI_URL)
            Log(L"  최신 판 주소를 받았습니다.");
        if (useUrl.empty()) {
            useUrl = OPTI_URL;
            Log(L"  최신 판 확인에 실패해 프로그램에 적힌 주소를 씁니다.");
        }
        Log(L"  주소: " + useUrl);
        Log(L"  저장 위치: " + zip);
        Status(L"OptiScaler 내려받는 중입니다...");
        HRESULT hr = URLDownloadToFileW(NULL, useUrl.c_str(), zip.c_str(), 0, NULL);
        if (FAILED(hr)) {
            Log(L"[실패] 내려받지 못했습니다. 인터넷 연결을 확인하고 다시 눌러 주세요.");
            Status(L"내려받기 실패");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }
        if (FileSizeOf(zip) < 50LL * 1024 * 1024) {
            Log(L"[실패] 받은 파일이 너무 작습니다. 내려받다 끊긴 것 같습니다.");
            DeleteFileW(zip.c_str());
            Status(L"내려받기 실패");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }
        Log(L"  → 내려받기를 마쳤습니다.");
    }

    Log(L"압축을 풀고 있습니다. 윈도우에 들어 있는 기능을 씁니다.");
    Status(L"압축을 푸는 중입니다...");
    RemoveTree(ex); CreateDirectoryW(ex.c_str(), NULL);
    std::wstring cmd = L"tar.exe -xf \"" + zip + L"\" -C \"" + ex + L"\"";
    if (!RunHidden(cmd, tmp) || !DirExists(ex)) {
        Log(L"[실패] 압축을 풀지 못했습니다. 윈도우 10 이상인지 확인해 주세요.");
        Status(L"압축 풀기 실패");
        delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
    }
    std::wstring srcRoot = ex;
    {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((ex + L"\\*").c_str(), &fd);
        int files = 0, dirs = 0; std::wstring only;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { dirs++; only = n; }
                else files++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (files == 0 && dirs == 1) srcRoot = ex + L"\\" + only;
    }
    Log(L"  → 압축을 풀었습니다.");

    Log(L"이 게임이 읽어 들일 파일 이름을 정하고 있습니다...");
    bool vulkanGuess = false;
    std::wstring proxy = ChooseProxyName(gameDir, vulkanGuess);
    Log(L"  → " + proxy + (vulkanGuess ? L" (불칸 게임이라 이 이름을 씁니다)" : L""));

    Log(L"기존 파일을 백업하고 있습니다.");
    Log(L"  백업 위치: " + backup);
    CreateDirectoryW(backup.c_str(), NULL);
    // 처음 백업본을 절대 덮어쓰지 않는다. 두 번째 설치부터는 이미 우리가 바꾼 파일이라
    // 그대로 덮으면 사용자의 원래 설정이 영영 사라진다.
    //
    // 2026-10-24 (1.0.3.2). 우리 파일은 백업하지 않는다.
    // 까닭과 실제 피해는 IsOurBackupCandidate 주석에 적어 두었다.
    const wchar_t* keep[] = { L"OptiScaler.ini", L"nvngx_dlss.dll" };
    for (int i = 0; i < 3; i++) {
        std::wstring name = (i < 2) ? std::wstring(keep[i]) : proxy;
        std::wstring from = gameDir + L"\\" + name;
        std::wstring to   = backup  + L"\\" + name;
        if (!FileExists(from)) continue;
        if (FileExists(to)) { Log(L"  이미 백업돼 있어 그대로 둡니다: " + name); continue; }
        if (IsOurBackupCandidate(from, name)) {
            Log(L"  우리가 넣은 파일이라 백업하지 않습니다: " + name);
            continue;
        }
        if (CopyFileW(from.c_str(), to.c_str(), TRUE)) {
            Log(L"  백업함: " + name);
        } else {
            wchar_t eb[200];
            swprintf_s(eb, L"  [경고] 백업하지 못했습니다 (윈도우 오류 %lu): ", GetLastError());
            Log(std::wstring(eb) + name);
        }
    }

    // 폴더도 통째로 백업한다. 기존에 OptiScaler 를 쓰시던 분의 파일이 들어 있을 수 있다.
    {
        const wchar_t* dirs[] = { L"OptiScaler", L"Licenses" };
        for (int i = 0; i < 2; i++) {
            std::wstring from = gameDir + L"\\" + dirs[i];
            std::wstring to   = backup  + L"\\" + dirs[i];
            if (!DirExists(from)) continue;
            if (DirExists(to)) { Log(std::wstring(L"  이미 백업돼 있어 그대로 둡니다: ") + dirs[i]); continue; }
            Status(L"기존 폴더를 백업하는 중입니다...");
            std::vector<std::wstring> got;
            CopyTree(from, to, got);
            wchar_t b[160];
            swprintf_s(b, L"  백업함: %s 폴더 (파일 %d개)", dirs[i], (int)got.size());
            Log(b);
        }
    }
    Log(L"게임 폴더에 파일을 넣고 있습니다...");
    Status(L"게임 폴더에 파일을 넣는 중입니다...");
    // backup 을 넘기면, 덮어쓸 자리에 있던 게임 파일을 먼저 그리로 옮긴다.
    CopyTree(srcRoot, gameDir, added, L"", backup);
    wchar_t cntMsg[128]; swprintf_s(cntMsg, L"  → 파일 %d개를 넣었습니다.", (int)added.size());
    Log(cntMsg);

    std::wstring optidll = gameDir + L"\\OptiScaler.dll";
    if (FileExists(optidll)) {
        if (!CopyFileW(optidll.c_str(), (gameDir + L"\\" + proxy).c_str(), FALSE)) {
            wchar_t eb[220];
            swprintf_s(eb, L"[실패] OptiScaler 를 %s 로 넣지 못했습니다 (윈도우 오류 %lu)",
                       proxy.c_str(), GetLastError());
            Log(eb);
            Log(L"       게임이 켜져 있거나 다른 프로그램이 그 파일을 잡고 있는지 확인해 주세요.");
            Status(L"설치 실패");
            delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0); return 0;
        }
        DeleteFileW(optidll.c_str());
        added.push_back(proxy);
        Log(L"  OptiScaler.dll 을 " + proxy + L" 로 바꿨습니다.");
    } else {
        Log(L"[경고] OptiScaler.dll 이 없습니다. 배포 파일 구성이 바뀐 것 같습니다.");
    }

    // 2026-10-23 바이오하자드 RE4 에서 드러났다.
    // exe 는 DLSS 를 제대로 쓰는데(via.render.DLSSUpscalingInterface, isDLSSAvailable)
    // 스팀판에 런타임 DLL 이 안 들어 있다. 그러면 OptiScaler 가
    //   "nvngx_dlss.dll not found, disabling DLSS"
    // 하고 업스케일러를 스스로 꺼서, 올라와도 할 일이 없어 창조차 안 열린다.
    // 리세이드 길에서만 넣던 파일을 이쪽에도 넣는다.
    {
        std::wstring dlssDst = gameDir + L"\\" + DLSS_DLL;
        // 게임이 다른 폴더에 자기 런타임을 두는 경우가 있다.
        //   킹덤컴2  실행 파일은 Bin\Win64MasterMasterSteamPGO
        //            런타임은  Bin\Win64Shared\nvngx_dlss.dll (48,971,808)
        // 그럴 때 실행 파일 옆에 우리 것을 하나 더 놓으면, 윈도우가 이름으로 찾을 때
        // 실행 파일 폴더를 먼저 보므로 게임이 고른 판본 대신 우리 것이 올라간다.
        // 부탁하지도 않은 교체가 된다. 게임 폴더 어디에도 없을 때만 넣는다.
        // 2026-10-23. 넣기 전에 대표님이 짚어 주셨다.
        std::wstring elsewhereRt;
        bool gameHasItSomewhere = FindFileDeep(job->dir, DLSS_DLL, 4, elsewhereRt)
                                  && !IsOurDlssRuntime(elsewhereRt);
        if (FileExists(dlssDst)) {
            Log(L"  DLSS 런타임은 이미 있어 건드리지 않습니다.");
        } else if (gameHasItSomewhere) {
            Log(L"  DLSS 런타임이 게임의 다른 폴더에 있어 넣지 않습니다.");
            Log(L"    " + elsewhereRt);
        } else {
            std::wstring dlssSrc = SelfDir() + L"\\" + DATA_DIR + L"\\" + FEED_DIR + L"\\" + DLSS_DLL;
            if (!FileExists(dlssSrc)) {
                Log(L"  [경고] 같이 들어 있어야 할 nvngx_dlss.dll 을 찾지 못했습니다.");
            } else {
                Status(L"DLSS 런타임을 넣는 중입니다...");
                std::wstring tmpDst = dlssDst + L".part";
                DeleteFileW(tmpDst.c_str());
                long long want = FileSizeOf(dlssSrc);
                bool ok = CopyFileW(dlssSrc.c_str(), tmpDst.c_str(), FALSE) != 0;
                if (ok && want > 0 && FileSizeOf(tmpDst) != want) { ok = false; }
                if (ok) { ok = MoveFileW(tmpDst.c_str(), dlssDst.c_str()) != 0; }
                if (ok) {
                    added.push_back(DLSS_DLL);
                    Log(L"  이 게임에는 DLSS 런타임이 없어 같이 넣었습니다. (nvngx_dlss.dll)");
                } else {
                    DeleteFileW(tmpDst.c_str());
                    wchar_t eb[200];
                    swprintf_s(eb, L"  [경고] DLSS 런타임을 넣지 못했습니다 (윈도우 오류 %lu)", GetLastError());
                    Log(eb);
                }
            }
        }
    }

    Log(L"DLSS 5 설정과 화질 기본값(Natural, 1패스)을 넣고 있습니다.");
    ApplyPreset(gameDir + L"\\OptiScaler.ini");

    SetupStreamlineForFg(gameDir, added);

    // 리세이드가 먼저 깔려 있으면 리세이드가 DXGI 팩토리를 감싸 버려서
    // OptiScaler 의 훅이 실패한다(로그에 Failed to hook IDXGIFactory / DLSS: 10DD,
    // 그 뒤로 초당 미니덤프). 이때는 훅 대신 감싸기로 들어가야 한다.
    // 2026-09-16 위처3(DX12 + 리세이드 6.7.3 dxgi.dll)에서 확인.
    if (ReShadeManagerHasGame(gameDir)) {
        Log(L"리세이드 매니저가 이 게임을 이미 들고 있습니다. 우리 리세이드는 넣지 않습니다.");
        Log(L"  (두 벌이 올라오면 나중 것이 스스로 꺼져서 창이 안 열립니다)");
        RemoveOurReShade(gameDir);
        // dll 은 매니저 것을 쓰더라도 효과 파일은 있어야 쓸 수 있다. 없으면 넣어준다.
        {
            std::wstring shDst = gameDir + L"\\" + RS_SHADERS;
            std::wstring shSrc = SelfDir() + L"\\" + DATA_DIR + L"\\" + RS_DIR + L"\\" + RS_SHADERS;
            if (!HasFxFiles(shDst, 3) && DirExists(shSrc)) {
                std::vector<std::wstring> got;
                CopyTree(shSrc, shDst, got);
                // 설치 기록에 적지 않는다. 까닭은 위 InstallReShadeBeside 와 같다. (2026-10-25)
                wchar_t b[160];
                swprintf_s(b, L"  효과 파일은 넣어 드립니다. (파일 %d개)", (int)got.size());
                Log(b);
            }
        }
        // 2026-10-23 레데리2. 매니저가 들고 있는 리세이드라도 ReShade.ini 는 게임 폴더에 있다.
        // 캐시 자리를 안 잡아 주면 이 게임만 계속 임시 폴더에 쌓인다.
        // 창 여는 키도 남의 ini 에 맞춰 넣고 있으니 같은 범위다.
        SetReShadeCachePath(gameDir + L"\\" + RS_INI);
    } else if (SendMessageW(g_hRs, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        Log(L"리세이드도 같이 넣습니다. (나중에 직접 넣으려 하면 이름이 겹쳐 막힙니다)");
        InstallReShadeBeside(gameDir, proxy, added);
    } else {
        Log(L"리세이드는 넣지 않습니다. (창에서 끄셨습니다)");
        RemoveOurReShade(gameDir);
    }

    // DxgiFactoryWrapping 은 끈다.
    //
    // 9월 13일 첫 릴리즈와 18일 사이에 "리세이드와 충돌을 피한다"며 켜 두었는데,
    // 왜 켜는지 소스에도 기록에도 한 줄도 안 남겨 두었다.
    //
    // 2026-10-25. "게임 설정을 누르면 한참 멈춘다" 는 제보 두 건을 위쳐3에서 재현했다.
    //   바닐라                      설정이 바로 열린다
    //   설치 후                     20초 멈춘다
    //   이 값만 false 로            설정이 바로 열린다
    // 게임이 설정 화면에서 CreateDXGIFactory1 을 0.1초마다 200번 부르는데,
    // 켜 두면 그때마다 옵티가 WrappedIDXGIFactory7 을 새로 만든다.
    // 30초에 200개를 만들고 18개만 놓았다.
    // 옵티 쪽 설명도 "문제를 일으킬 수 있다" 이고 기본값은 꺼짐이다.
    //
    // 줄을 지우지 않고 false 를 써 넣는다. 옛 판이 이미 true 를 적어 둔 분들이
    // 있어서, 안 쓰면 그분들 ini 에 true 가 그대로 남는다.
    //
    // 2026-10-27. 여기에 HasReShade(gameDir) 조건이 붙어 있었다. 그러면
    // 리세이드를 리세이드 매니저(전역 주입, C:\ProgramData\ReShade)로 쓰시는 분은
    // 게임 폴더에 리세이드가 없어서 이 줄을 안 쓴다. 옛 판이 적어 둔 true 가
    // 그대로 남아 설정 화면이 계속 20초 멈춘다. 레데리2 에서 실제로 그랬다.
    // 조건을 뗀다. 옵티를 넣는 자리면 언제나 쓴다.
    Log(L"  DxgiFactoryWrapping 을 끕니다. (켜 두면 게임 설정 화면이 20초쯤 멈춥니다)");
    SetIniKey(gameDir + L"\\OptiScaler.ini", "Spoofing", "DxgiFactoryWrapping", "false");

    }

    Status(L"신경망 파일을 확인하는 중입니다...");
    std::wstring nr;
    bool nrReady = false;
    bool haveNr = FindNeuralDll(gameDir, nr);
    if (haveNr) {
        if (LowerW(nr) != LowerW(gameDir + L"\\" + NR_DLL)) {
            Log(L"  찾았습니다: " + nr);
            Log(L"  이 파일을 복사해 옵니다. 원래 있던 곳은 그대로 둡니다.");
            Log(L"  복사 중입니다. 150MB가 넘어 잠시 걸립니다.");
            Status(L"신경망 파일을 복사하는 중입니다...");
            std::wstring nrDst = gameDir + L"\\" + NR_DLL;
            std::wstring nrTmp = nrDst + L".part";
            long long srcSize = FileSizeOf(nr);
            DeleteFileW(nrTmp.c_str());
            bool copied = CopyFileW(nr.c_str(), nrTmp.c_str(), FALSE) != 0;
            DWORD err = copied ? 0 : GetLastError();
            if (copied && srcSize > 0 && FileSizeOf(nrTmp) != srcSize) {
                copied = false; err = ERROR_HANDLE_DISK_FULL;
            }
            if (copied) {
                DeleteFileW(nrDst.c_str());
                if (!MoveFileW(nrTmp.c_str(), nrDst.c_str())) { copied = false; err = GetLastError(); }
            }
            if (copied) {
                SaveNrPath(nr);
                added.push_back(NR_DLL);
                nrReady = true;
                Log(L"  → 복사를 마쳤습니다. 이 위치를 기억해 두었으니 다음 게임부터는 바로 씁니다.");
                Log(L"     원본 : " + FileFingerprint(nr));
                Log(L"     설치 : " + FileFingerprint(nrDst));
                Log(L"     (CRC 가 서로 같아야 정상입니다. 카드에 맞지 않는 파일이면 게임에서 켜지지 않습니다)");
            } else {
                DeleteFileW(nrTmp.c_str());
                wchar_t eb[200];
                swprintf_s(eb, L"[실패] 신경망 파일을 넣지 못했습니다. (윈도우 오류 %lu)", err);
                Log(eb);
                Log(L"       원본 위치: " + nr);
                Log(L"       넣으려던 곳: " + nrDst);
                if (err == ERROR_HANDLE_DISK_FULL || err == ERROR_DISK_FULL)
                    Log(L"       복사가 중간에 끊겼습니다. 드라이브 여유 공간을 확인해 주세요.");
                else if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION)
                    Log(L"       게임이나 런처가 파일을 잡고 있습니다. 전부 끄고 다시 눌러 주세요.");
                else if (err == ERROR_ACCESS_DENIED)
                    Log(L"       권한이 막혔습니다. 이 프로그램을 오른쪽 클릭 → [관리자 권한으로 실행] 해 주세요.");
            }
        } else {
            Log(L"  이미 게임 폴더에 있습니다.");
            nrReady = true;
        }
    } else {
        Log(L"");
        Log(L"");
        Log(L"[중요] 신경망 파일(nvngx_dlssnr.dll)을 끝내 찾지 못했습니다.");
        Log(L"       설치는 여기까지 됐고, DLSS 업스케일링은 동작합니다.");
        Log(L"       신경망 렌더링만 아직 꺼져 있습니다.");
        Log(L"       이 파일은 받으신 압축 안에 프로그램과 함께 들어 있습니다.");
        Log(L"       압축을 푸신 폴더에서 DLSS5_Setup.exe 를 실행해 주세요.");
        Log(L"       (exe 만 따로 옮기시면 옆에 있던 파일을 찾지 못합니다)");
        Log(L"       지금 프로그램 위치: " + SelfDir());
    }

    if (!nrReady) {
        if (SetIniKey(gameDir + L"\\OptiScaler.ini", "DlssNr", "Enabled", "false")) {
            Log(L"");
            Log(L"  신경망 파일이 없어서 DLSS Neural Rendering 항목은 꺼 두었습니다.");
            Log(L"  (켠 채로 두면 게임이 매 프레임 헛돌며 로그가 불어나고 프레임이 떨어집니다)");
            Log(L"  나중에 파일을 받으신 뒤 [DLSS 5 설치]를 다시 누르면 자동으로 켜집니다.");
        }
    }

    std::string man;
    for (size_t i = 0; i < movedAside.size(); i++) man += "MOVED " + W2U(movedAside[i]) + "\r\n";
    for (size_t i = 0; i < added.size(); i++) man += W2U(added[i]) + "\r\n";
    WriteFileBytes(gameDir + L"\\" + MANIFEST, man);
    Log(L"설치 기록을 남겼습니다. [되돌리기]를 누르면 이 목록대로 원래대로 돌려놓습니다.");

    // 창에서 바꾸신 단축키를 맨 마지막에 적는다. 앞 단계가 쓴 값을 덮어야 해서다.
    // 안 바꾸셨으면 아무것도 안 쓴다.
    ApplyHotKeys(gameDir, modeB);

    Log(L"───── 설치를 마쳤습니다");
    Log(L"");
    // 사람이 로그를 읽지 않아도 되도록, 설치가 제대로 됐는지 스스로 확인한다.
    Status(L"설치가 제대로 됐는지 확인하는 중입니다...");
    int checkMiss = 0;
    {
        std::wstring report;
        int miss = SelfCheck(gameDir, modeB, report);
        checkMiss = miss;
        Log(L"");
        if (miss == 0) {
            Log(L"───── 자체 점검: 이상 없습니다");
        } else {
            Log(L"───── 자체 점검: 빠진 것이 있습니다");
            Log(report);
            std::wstring NL(1, (wchar_t)10);
            std::wstring m;
            m += L"설치가 끝났지만 확인해 보니 빠진 것이 있습니다." + NL + NL;
            m += report + NL;
            m += L"이대로는 게임에서 DLSS 5가 켜지지 않습니다." + NL + NL;
            m += L"[되돌리기] 를 누르신 뒤 다시 설치해 보세요." + NL;
            m += L"그래도 같으면 [진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요." + NL;
            m += L"무엇이 빠졌는지 그 기록에 그대로 적혀 있습니다.";
            MessageBoxW(g_hWnd, m.c_str(), L"설치를 확인해 주세요",
                        MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
    }

    Log(L"★ 이제 게임에서 할 일 (순서대로 하셔야 켜집니다)");
    if (modeB) {
        // DLSS 가 없는 게임. 게임 설정에서 바꿀 것이 없다.
        Log(L"  1. 게임을 켭니다.");
        Log(L"     이 게임은 설정에 DLSS 항목이 없습니다. 그래서 바꾸실 것이 없습니다.");
        Log(L"     프로그램이 넣어 둔 것들이 알아서 DLSS 5를 만들어 겁니다.");
        Log(L"  2. 세이브를 불러와 실제 게임 화면으로 들어가세요. 메뉴 화면에서는 적용되지 않습니다.");
        Log(L"  3. " + HotKeyName(HK_RS_OVERLAY) + L" 키로 리세이드 창을 열고 위쪽 RenoDX-DLSSNR 칸을 누르세요.");
        Log(L"     ACTIVE - NR INJECTED 라고 적혀 있으면 켜진 것입니다.");
        Log(L"  4. " + HotKeyName(HK_DLSS_TOGGLE) + L" 키로 껐다 켜며 비교해 보세요. 얼굴 클로즈업에서 차이가 가장 잘 보입니다.");
    } else {
        Log(L"  1. 게임을 켭니다.");
        Log(L"  2. 게임 설정 → 그래픽 → 업스케일링(Upscaling)을 반드시 DLSS로 바꿉니다.");
        Log(L"     이걸 안 하면 DLSS 5는 켜지지 않습니다. FSR 이나 꺼짐으로 두면 안 됩니다.");
        Log(L"  3. 세이브를 불러와 실제 게임 화면으로 들어가세요. 메뉴 화면에서는 적용되지 않습니다.");
        Log(L"  4. " + HotKeyName(HK_DLSS_PANEL) + L" 키로 조절 창을 열고 맨 아래 DLSS Neural Rendering 항목을 펼치세요.");
        Log(L"  5. " + HotKeyName(HK_DLSS_TOGGLE) + L" 키로 껐다 켜며 비교해 보세요. 얼굴 클로즈업에서 차이가 가장 잘 보입니다.");
        Log(L"");
        // 2026-10-23 레데리2. 리세이드 매니저가 이 게임을 들고 있으면 우리 것을 안 넣는데,
        // 안내는 넣었다고 적고 있었다. 안 한 일을 했다고 적으면 안 된다.
        if (ReShadeManagerHasGame(gameDir)) {
            Log(L"※ 리세이드는 리세이드 매니저가 이미 들고 있어 우리 것을 넣지 않았습니다.");
            Log(L"   창 여는 키는 매니저에서 정하신 그대로입니다.");
        } else {
            Log(L"※ 리세이드도 같이 넣었습니다. " + HotKeyName(HK_RS_OVERLAY)
                + L" 키로 열립니다. (DLSS 5 창은 " + HotKeyName(HK_DLSS_PANEL) + L" 입니다)");
            Log(L"   8 을 눌러도 창이 안 뜨면 [DLSS 5 설치]를 한 번 더 누르세요.");
            Log(L"   게임마다 읽어 들이는 파일 이름이 달라서, 다른 이름으로 바꿔 넣습니다.");
        }
        Log(L"※ 프레임이 모자라면 " + HotKeyName(HK_DLSS_PANEL) + L" 창에서 Model resolution 을 0.7 로 내리세요.");
        // 스트림라인 폴더를 넣었다는 건 이 게임이 DLSS 프레임 생성을 할 줄 안다는 뜻이다.
        // 게임 설정에서 켜야 호출이 생기고, 그래야 옵티가 가로챌 것이 있다.
        if (DirExists(gameDir + L"\\OptiScaler\\streamline")) {
            Log(L"※ 이 게임은 DLSS 프레임 생성을 할 줄 압니다. 쓰시려면 이렇게 하세요.");
            Log(L"   1. 게임 설정에서 DLSS 프레임 생성을 켭니다.");
            Log(L"   2. 그것만으로 프레임이 오르면 거기서 끝입니다. 창은 건드리지 마세요.");
            Log(L"   3. 안 오를 때만 " + HotKeyName(HK_DLSS_PANEL) + L" 창의 Frame Generation 을 DLSSG 로 바꾸세요.");
            Log(L"   수직동기화를 켜 두면 주사율에서 막혀 숫자가 안 오릅니다.");
        }
    }
    Log(L"※ 피부가 하얗게 뜨거나 반짝이면 Detail 과 Colour 를 내리세요.");
    Log(L"※ 설치 직후 첫 실행은 실패할 수 있습니다. 그냥 한 번 더 켜시면 됩니다.");
    Log(L"   엔비디아가 뒤에서 파일을 정리하느라 그런 것이라 고장이 아닙니다.");
    // 2026-10-23. REFramework 을 꺼 둔 경우에는 끝 안내에도 적는다.
    // 진행 기록은 잘 안 보시는데, 모드가 안 뜨면 우리 탓으로 돌아온다.
    if (FileExists(gameDir + L"\\dinput8.dll.off")) {
        Log(L"※ REFramework 을 잠시 꺼 두었습니다. (dinput8.dll → dinput8.dll.off)");
        Log(L"   둘을 같이 올리면 게임을 시작하는 순간 꺼집니다. 제작자 쪽에도 같은 제보가 많습니다.");
        Log(L"   REFramework 을 쓰셔야 하면 [되돌리기] 를 누르면 원래대로 돌아갑니다.");
    }
    Log(L"※ 게임이 안 켜지거나 이상하면 [되돌리기]를 누르면 설치 전으로 돌아갑니다.");
    Log(L"");
    if (checkMiss == 0) {
        Log(L"══════════════════════════════════════════════");
        Log(L"        설치가 완료되었습니다. 이제 게임을 켜세요.");
        Log(L"══════════════════════════════════════════════");
        Status(L"설치가 완료되었습니다");
    } else {
        Log(L"══════════════════════════════════════════════");
        Log(L"        설치를 마쳤지만 빠진 것이 있습니다.");
        Log(L"        위의 [자체 점검] 부분을 확인해 주세요.");
        Log(L"══════════════════════════════════════════════");
        Status(L"확인이 필요합니다");
    }
    delete job; PostMessageW(g_hWnd, WM_DONE, 0, 0);
    return 0;
}

// ================================================================ 창
static void AddLogLine(const wchar_t* s) {
    std::wstring line = L"[" + NowStamp() + L"] " + s + L"\r\n";
    int len = GetWindowTextLengthW(g_hLog);

    // 창이 다 차면 앞쪽을 잘라낸다. 한도에 닿으면 뒤가 안 들어가고 멈춘다.
    // 자를 때는 줄 가운데가 아니라 줄 끝에서 자른다. (2026-10-25)
    if (len > LOG_UI_MAX - LOG_UI_TRIM) {
        int keepFrom = LOG_UI_TRIM;
        std::vector<wchar_t> buf((size_t)len + 1);
        GetWindowTextW(g_hLog, &buf[0], len + 1);
        std::wstring all(&buf[0]);
        size_t nl = all.find(L"\r\n", (size_t)keepFrom);
        all = (nl == std::wstring::npos) ? std::wstring() : all.substr(nl + 2);
        SetWindowTextW(g_hLog, all.c_str());
        len = GetWindowTextLengthW(g_hLog);
    }

    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
    InvalidateRect(g_hLog, NULL, TRUE);
    UpdateWindow(g_hLog);
    if (!g_logPath.empty()) AppendFileBytes(g_logPath, W2U(line));
}
static std::wstring PickFolder(HWND owner) {
    BROWSEINFOW bi = {};
    wchar_t disp[MAX_PATH] = {};
    bi.hwndOwner = owner; bi.pszDisplayName = disp;
    bi.lpszTitle = L"게임이 설치된 폴더를 고르세요";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

// 고른 게임에 맞춰 "리세이드도 같이 설치" 칸을 손본다. (2026-10-23)
//
// DLSS 가 없는 게임은 리세이드 애드온 위에서 신경망이 돈다. 리세이드가 본체라
// 그 칸을 끄면 아무것도 동작하지 않는 설치가 된다. 전에는 끄셨어도 말없이 넣었는데,
// 끌 수 있게 보여 주고 무시하는 것보다 처음부터 못 끄게 하는 편이 정직하다.
//
// 판별은 설치할 때와 똑같은 GameSupportsDlssFor 를 쓴다. 두 군데서 다르게 판단하면
// 창에 보이는 것과 실제로 하는 일이 어긋난다. 다만 고를 때마다 진행 기록이
// 쌓이면 안 되므로 그 동안만 기록을 잠근다.
// 단축키 칸 ──────────────────────────────────────────────
// 콤보에서 고른 키의 VK 값. 못 고른 상태면 기본값을 돌려준다.
static int HotKeyVk(int which) {
    if (which < 0 || which >= HK_N || !g_hKeyBox[which]) { return HK_DEFAULT[which]; }
    int i = (int)SendMessageW(g_hKeyBox[which], CB_GETCURSEL, 0, 0);
    if (i < 0 || i >= KEY_COUNT) { return HK_DEFAULT[which]; }
    return KEY_LIST[i].vk;
}

static std::wstring HotKeyName(int which) {
    int vk = HotKeyVk(which);
    for (int i = 0; i < KEY_COUNT; i++) {
        if (KEY_LIST[i].vk == vk) { return KEY_LIST[i].name; }
    }
    return L"?";
}

// VK 값을 콤보의 몇 번째 줄인지로 바꾼다. 목록에 없는 값이면 -1.
static int VkToRow(int vk) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (KEY_LIST[i].vk == vk) { return i; }
    }
    return -1;
}

// 고른 게임의 파일에서 지금 값을 읽어 네 칸에 띄운다.
// 읽은 값을 그대로 두면 설치할 때 안 쓰므로, 손대지 않으면 아무 일도 안 일어난다.
static void LoadHotKeys(const std::wstring& gameDir) {
    int vk[HK_N];
    for (int k = 0; k < HK_N; k++) { vk[k] = HK_DEFAULT[k]; }

    // 2026-10-27. 고른 게임이 바뀌면 "직접 고르셨다" 는 표시를 지운다.
    // 그래야 새 게임의 지금 값이 칸에 뜬다. 같은 게임에 머무는 동안에는
    // (리세이드 체크를 누르거나 같은 줄을 다시 눌러도) 고르신 것이 그대로 남는다.
    {
        static std::wstring lastDir = L"\x01";
        if (LowerW(gameDir) != LowerW(lastDir)) {
            lastDir = gameDir;
            for (int k = 0; k < HK_N; k++) { g_hkTouched[k] = false; }
        }
    }

    if (!gameDir.empty()) {
        std::wstring rs = gameDir + L"\\" + RS_INI;
        std::string s;
        // "56,0,0,0" 처럼 적힌다. 앞의 숫자만 쓴다.
        s = GetIniKey(rs, "INPUT", "KeyOverlay");
        if (!s.empty()) { vk[HK_RS_OVERLAY] = atoi(s.c_str()); }
        s = GetIniKey(rs, "INPUT", "KeyEffects");
        if (!s.empty()) { vk[HK_RS_EFFECTS] = atoi(s.c_str()); }
        // "118" 처럼 10진수로 적힌다.
        s = GetIniKey(rs, "RenoDX.DLSS5", "NRToggleKey");
        if (!s.empty()) { vk[HK_DLSS_TOGGLE] = atoi(s.c_str()); }

        // 옵티는 "0x2D" 처럼 16진수다. "auto" 면 기본값이라는 뜻이다.
        std::wstring op = gameDir + L"\\OptiScaler.ini";
        s = GetIniKey(op, "Menu", "ShortcutKey");
        if (!s.empty() && s != "auto") { vk[HK_DLSS_PANEL] = (int)strtol(s.c_str(), NULL, 0); }
        s = GetIniKey(op, "DlssNr", "ToggleKey");
        if (!s.empty() && s != "auto") { vk[HK_DLSS_TOGGLE] = (int)strtol(s.c_str(), NULL, 0); }
    }

    for (int k = 0; k < HK_N; k++) {
        int row = VkToRow(vk[k]);
        if (row < 0) { row = VkToRow(HK_DEFAULT[k]); vk[k] = HK_DEFAULT[k]; }
        // 그 게임 파일에 적혀 있던 값. 설치할 때 이것과 다르면 쓴다.
        g_hkLoaded[k] = vk[k];
        // 2026-10-27. 직접 고르신 칸은 게임을 바꿔도 건드리지 않는다.
        // 전에는 목록을 누를 때마다 파일 값으로 되돌아가서 고른 것이 날아갔다.
        if (g_hkTouched[k]) { continue; }
        if (!g_hKeyBox[k]) { continue; }
        SendMessageW(g_hKeyBox[k], CB_SETCURSEL, (WPARAM)row, 0);
    }
}

// 대표님이 그 칸을 바꾸셨나. 안 바꾸셨으면 그 파일을 건드리지 않는다.
static bool HotKeyChanged(int which) {
    return HotKeyVk(which) != g_hkLoaded[which];
}

// 설치가 끝난 뒤, 바꾸신 칸만 파일에 쓴다.
// 파일마다 적는 모양이 달라서 여기 한 곳에 모아 두었다.
static void ApplyHotKeys(const std::wstring& gameDir, bool modeB) {
    std::wstring rsIni = gameDir + L"\\" + RS_INI;
    std::wstring opIni = gameDir + L"\\OptiScaler.ini";
    char buf[64];

    if (HotKeyChanged(HK_RS_OVERLAY) && FileExists(rsIni)) {
        sprintf_s(buf, "%d,0,0,0", HotKeyVk(HK_RS_OVERLAY));
        if (SetIniKey(rsIni, "INPUT", "KeyOverlay", buf)) {
            Log(L"  리세이드 창 키를 바꿨습니다: " + std::wstring(KEY_LIST[VkToRow(HotKeyVk(HK_RS_OVERLAY))].name));
        }
    }
    if (HotKeyChanged(HK_RS_EFFECTS) && FileExists(rsIni)) {
        sprintf_s(buf, "%d,0,0,0", HotKeyVk(HK_RS_EFFECTS));
        if (SetIniKey(rsIni, "INPUT", "KeyEffects", buf)) {
            Log(L"  리세이드 효과 끄는 키를 바꿨습니다: " + std::wstring(KEY_LIST[VkToRow(HotKeyVk(HK_RS_EFFECTS))].name));
        }
    }
    // DLSS 5 창은 옵티 길에만 있다. 리세이드 길은 리세이드 창 안에 들어 있다.
    if (!modeB && HotKeyChanged(HK_DLSS_PANEL) && FileExists(opIni)) {
        sprintf_s(buf, "0x%02X", HotKeyVk(HK_DLSS_PANEL));
        if (SetIniKey(opIni, "Menu", "ShortcutKey", buf)) {
            Log(L"  DLSS 5 창 키를 바꿨습니다: " + std::wstring(KEY_LIST[VkToRow(HotKeyVk(HK_DLSS_PANEL))].name));
        }
    }
    if (HotKeyChanged(HK_DLSS_TOGGLE)) {
        std::wstring nm = KEY_LIST[VkToRow(HotKeyVk(HK_DLSS_TOGGLE))].name;
        if (modeB && FileExists(rsIni)) {
            sprintf_s(buf, "%d", HotKeyVk(HK_DLSS_TOGGLE));
            if (SetIniKey(rsIni, "RenoDX.DLSS5", "NRToggleKey", buf)) {
                Log(L"  DLSS 5 껐다 켜는 키를 바꿨습니다: " + nm);
            }
        } else if (!modeB && FileExists(opIni)) {
            sprintf_s(buf, "0x%02X", HotKeyVk(HK_DLSS_TOGGLE));
            if (SetIniKey(opIni, "DlssNr", "ToggleKey", buf)) {
                Log(L"  DLSS 5 껐다 켜는 키를 바꿨습니다: " + nm);
            }
        }
    }
}

static void UpdateReShadeCheckbox(const std::wstring& gameRoot) {
    if (!g_hRs) { return; }
    bool mustHave = false;
    if (!gameRoot.empty()) {
        std::wstring exe = FindGameExe(ResolveExeDir(gameRoot));
        if (!exe.empty()) {
            g_logMuted = true;
            int sup = GameSupportsDlssFor(exe);
            g_logMuted = false;
            mustHave = (sup == 0);
        }
    }
    if (mustHave) {
        SendMessageW(g_hRs, BM_SETCHECK, BST_CHECKED, 0);
        EnableWindow(g_hRs, FALSE);
        SetWindowTextW(g_hRs, L"리세이드도 같이 설치 (이 게임은 리세이드가 있어야 동작합니다)");
    } else {
        EnableWindow(g_hRs, TRUE);
        SetWindowTextW(g_hRs, L"리세이드도 같이 설치 (끄면 DLSS 5 만 넣습니다)");
    }
}

// 목록에서 지금 고른 게임으로 위 칸을 맞춘다.
static void SyncReShadeCheckboxToSelection() {
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_games.size()) {
        UpdateReShadeCheckbox(L"");
        LoadHotKeys(L"");
        return;
    }
    UpdateReShadeCheckbox(g_games[sel].dir);
    // 그 게임이 쓰던 키를 칸에 띄운다. 실행 파일이 하위 폴더에 있는 게임이
    // 있어서, 설치할 때와 같은 자리를 봐야 한다.
    LoadHotKeys(ResolveExeDir(g_games[sel].dir));
}

// 목록에 없는 게임을 사용자가 직접 골라 넣는다.
// 스팀 말고 GOG·에픽·엑스박스 같은 데 깔린 게임을 이 길로 받는다.
// 고르는 것은 게임 루트 폴더다. 실행 파일이 몇 단계 아래에 있든 FindGameExe 가 찾는다.
static void PickGameFolder() {
    if (g_busy) return;
    std::wstring dir = PickFolder(g_hWnd);
    if (dir.empty()) return;
    AddLogLine((L"고른 폴더: " + dir).c_str());
    while (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/'))
        dir.erase(dir.size() - 1);

    for (size_t i = 0; i < g_games.size(); i++) {
        if (LowerW(g_games[i].dir) == LowerW(dir)) {
            SendMessageW(g_hList, LB_SETCURSEL, i, 0);
            SyncReShadeCheckboxToSelection();
            AddLogLine(L"  이미 목록에 있는 게임입니다.");
            SetWindowTextW(g_hStatus, L"이미 목록에 있는 게임입니다.");
            return;
        }
    }

    std::wstring exe = FindGameExe(dir);
    if (exe.empty()) {
        AddLogLine((L"고른 폴더에서 게임 실행 파일을 찾지 못했습니다: " + dir).c_str());
        AddLogLine(L"  게임이 설치된 폴더를 고르세요. 런처 폴더나 상위 폴더가 아닙니다.");
        SetWindowTextW(g_hStatus, L"이 폴더에서는 게임을 찾지 못했습니다.");
        return;
    }

    size_t sl = dir.find_last_of(L"\\");
    std::wstring name = (sl == std::wstring::npos) ? dir : dir.substr(sl + 1);

    Game g; g.name = name + L"  (직접 고름)"; g.dir = dir;
    g_games.push_back(g);
    int idx = (int)SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)g.name.c_str());
    SendMessageW(g_hList, LB_SETCURSEL, idx, 0);
    SyncReShadeCheckboxToSelection();

    AddLogLine((L"직접 고른 게임: " + name).c_str());
    AddLogLine((L"  폴더: " + dir).c_str());
    AddLogLine((L"  실행 파일: " + exe).c_str());
    SetWindowTextW(g_hStatus, L"목록에 넣었습니다. [DLSS 5 설치] 를 누르세요.");
}
static void CopyLogToClipboard() {
    int len = GetWindowTextLengthW(g_hLog);
    std::wstring buf(len + 1, 0);
    GetWindowTextW(g_hLog, &buf[0], len + 1);
    buf.resize(len);

    // 사람들이 이 기록을 공개 댓글에 붙여넣는다. 경로에 든 윈도우 사용자 이름은 가린다.
    // 진단에는 필요 없는 정보다. USERPROFILE 의 마지막 폴더 이름이 경로에 찍히는 이름이다.
    {
        wchar_t prof[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH)) {
            std::wstring pf = prof;
            size_t sl = pf.find_last_of(L"\\");
            std::wstring name = (sl == std::wstring::npos) ? pf : pf.substr(sl + 1);
            if (!name.empty()) {
                std::wstring needle = LowerW(std::wstring(L"\\users\\") + name);
                std::wstring repl = L"\\Users\\(사용자)";
                std::wstring low = LowerW(buf);
                size_t pos = 0;
                while ((pos = low.find(needle, pos)) != std::wstring::npos) {
                    buf.replace(pos, needle.size(), repl);
                    low.replace(pos, needle.size(), LowerW(repl));
                    pos += repl.size();
                }
            }
        }
    }

    if (!OpenClipboard(g_hWnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (buf.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), buf.c_str(), (buf.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    AddLogLine(L"진행 기록을 복사했습니다. 댓글이나 메시지에 그대로 붙여넣기 하시면 됩니다.");
}
static void StartJob(bool restore) {
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_games.size()) {
        MessageBoxW(g_hWnd, L"먼저 목록에서 게임을 고르세요.",
                    L"DLSS 5 설치 도우미", MB_ICONINFORMATION);
        g_busy = 0; return;
    }
    AddLogLine((L"고른 게임: " + g_games[sel].name).c_str());

    // 엔비디아가 아니면 아예 켜지지 않는다. 설치해 봐야 파일만 남는다.
    if (!restore && !g_gpuNvidia) {
        MessageBoxW(g_hWnd,
        (L"이 PC 의 그래픽카드는 엔비디아가 아닙니다.\n\n"
        L"  찾은 것: " + g_gpuName + L"\n\n"
        L"DLSS 5 의 뉴럴 렌더링은 엔비디아가 만든 모델을 쓰고, 그 모델은 "
        L"엔비디아 드라이버 위에서만 돌아갑니다. 설치해도 켜지지 않고 "
        L"게임 폴더에 파일만 남습니다.\n\n"
        L"라데온에서 돌려 보려는 시도가 따로 있긴 합니다만, 제가 확인하지 "
        L"못해 이 도구에는 넣지 않았습니다.\n\n"
        L"설치하지 않습니다.").c_str(),
        L"엔비디아 카드가 아닙니다", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        AddLogLine((L"엔비디아 카드가 아니라 설치를 멈췄습니다: " + g_gpuName).c_str());
        EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE); EnableWindow(g_hPick, TRUE);
        g_busy = 0; return;
    }

    // RTX 20 / 30 은 될 수도 있고 안 될 수도 있다. 막지는 않되 미리 알린다.
    if (!restore && g_gpuNvidia && (g_gpuSeries == 2 || g_gpuSeries == 3)) {
        int a = MessageBoxW(g_hWnd,
        (L"RTX 20 / 30 시리즈에서는 확인하지 못했습니다.\n\n"
        L"  찾은 것: " + g_gpuName + L"\n\n"
        L"엔비디아가 공식으로 지원한다고 밝힌 것은 RTX 50 이고, "
        L"RTX 40 은 제가 확인했습니다. 20 과 30 은 켜지지 않거나 "
        L"프레임이 크게 떨어질 수 있습니다.\n\n"
        L"그래도 설치할까요? 안 되면 [되돌리기]로 원래대로 돌아갑니다.").c_str(),
        L"확인하지 못한 그래픽카드입니다", MB_OKCANCEL | MB_ICONQUESTION | MB_TOPMOST);
        if (a != IDOK) {
            AddLogLine(L"사용자가 설치를 취소했습니다 (RTX 20 / 30).");
            EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE); EnableWindow(g_hPick, TRUE);
            g_busy = 0; return;
        }
        AddLogLine(L"확인하지 못한 카드인데 사용자가 계속하기를 골랐습니다.");
    }

    // DLSS 1.0 에서 멈춘 게임들. nvngx_dlss.dll 은 있지만 요즘 DLSS 가 쓰는
    // 연결 통로가 없어서, 옵티스케일러가 가로챌 것이 없다. 설치는 되고 효과만 없다.
    if (!restore) {
        std::wstring gl = LowerW(g_games[sel].name);
        bool dlss1 = false;
        if (gl.find(L"final fantasy xv") != std::wstring::npos) dlss1 = true;
        if (gl.find(L"monster hunter") != std::wstring::npos &&
            gl.find(L"world") != std::wstring::npos) dlss1 = true;
        if (gl.find(L"battlefield") != std::wstring::npos) {
            // "Battlefield V" 와 "Battlefield(TM) V" 만 잡는다. 1 이나 2042 는 아니다.
            size_t n = gl.find_last_not_of(L" ");
            if (n != std::wstring::npos && gl[n] == L'v') dlss1 = true;
        }
        if (dlss1) {
            AddLogLine(L"  → DLSS 1.0 에서 멈춘 게임입니다. 신경망을 걸 통로가 없습니다.");
            int a = MessageBoxW(g_hWnd,
            (L"이 게임의 DLSS 는 DLSS 1.0 입니다.\n\n"
            L"  고른 게임: " + g_games[sel].name + L"\n\n"
            L"DLSS 1.0 은 지금의 DLSS 2 / 3 / 4 와 이름만 같고 구조가 다릅니다. "
            L"게임마다 따로 학습시켜 넣던 방식이라, 요즘 DLSS 가 쓰는 연결 통로가 "
            L"아예 없습니다.\n\n"
            L"이 프로그램은 그 통로를 가로채서 신경망을 거는 방식입니다. 가로챌 것이 "
            L"없으면 설치는 끝까지 잘 되지만 게임 안에서는 아무 일도 일어나지 "
            L"않습니다. 설정을 바꿔도 달라지지 않습니다.\n\n"
            L"그래도 설치할까요? [되돌리기]를 누르면 언제든 원래대로 돌아갑니다.").c_str(),
            L"DLSS 1.0 게임입니다", MB_OKCANCEL | MB_ICONWARNING | MB_TOPMOST);
            if (a != IDOK) {
                AddLogLine(L"사용자가 설치를 취소했습니다 (DLSS 1.0 게임).");
                EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE); EnableWindow(g_hPick, TRUE);
                g_busy = 0; return;
            }
            AddLogLine(L"DLSS 1.0 게임인데 사용자가 계속하기를 골랐습니다.");
        }
    }

    std::wstring online = FindOnlineGame(g_games[sel].dir);
    if (!online.empty() && !restore) {
        MessageBoxW(g_hWnd,
        (L"이 게임은 온라인 게임입니다.\n\n"
        L"  찾은 것: " + online + L"\n\n"
        L"이런 게임의 안티치트는 게임 안에 들어 있어 파일로는 보이지 않습니다. "
        L"DLSS 5 설치는 게임에 dll 을 끼워 넣는 방식이라, 안티치트가 보면 "
        L"치트 프로그램과 구분하지 못합니다. 계정이 영구 정지될 수 있습니다.\n\n"
        L"이 게임에는 설치하지 않습니다.").c_str(),
        L"온라인 게임입니다", MB_ICONWARNING);
        AddLogLine((L"온라인 게임이라 설치를 멈췄습니다: " + online).c_str());
        EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE); EnableWindow(g_hPick, TRUE);
        g_busy = 0; return;
    }
    std::wstring hit = FindAntiCheat(g_games[sel].dir, 5);
    if (!hit.empty() && !restore) {
        MessageBoxW(g_hWnd,
        (L"이 게임에는 안티치트가 들어 있습니다.\n\n"
        L"  찾은 것: " + hit + L"\n\n"
        L"DLSS 5 설치는 게임에 dll 을 끼워 넣는 방식이라, 안티치트가 보면 "
        L"치트 프로그램과 구분하지 못합니다. 계정이 정지될 수 있습니다.\n\n"
        L"이 게임에는 설치하지 않습니다.").c_str(),
        L"안티치트가 있는 게임입니다", MB_ICONWARNING);
        AddLogLine((L"안티치트가 있어 설치를 멈췄습니다: " + hit).c_str());
        EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE); EnableWindow(g_hPick, TRUE);
        g_busy = 0; return;
    }

    EnableWindow(g_hInstall, FALSE); EnableWindow(g_hRestore, FALSE);
    EnableWindow(g_hPick, FALSE);
    Job* job = new Job{ g_games[sel].dir, restore };
    CreateThread(NULL, 0, WorkThread, job, 0, NULL);
}

// 창 크기가 바뀔 때마다 안쪽 요소들을 다시 배치한다
static void LayoutControls(int w, int h) {
    const int M = 20;                       // 바깥 여백
    const int BTN_H = 38, ROW_GAP = 12;
    int cw = w - M * 2;
    if (cw < 300) cw = 300;

    MoveWindow(g_hTitle, M, 14, cw, 32, TRUE);
    MoveWindow(g_hDesc,  M, 50, cw, 78, TRUE);
    MoveWindow(g_hGpu,   M, 132, cw, 22, TRUE);

    int top = 160;
    int bottom = h - M;
    const int KEY_H = 26;   // 단축키 줄
    int fixedBlock = 24 + ROW_GAP + KEY_H + ROW_GAP + BTN_H + ROW_GAP + 22 + 8;   // 체크박스 + 단축키줄 + 버튼줄 + 상태줄
    int space = bottom - top - fixedBlock;
    if (space < 200) space = 200;
    int listH = (int)(space * 0.40);
    if (listH < 100) listH = 100;
    int logH = space - listH;
    if (logH < 100) logH = 100;

    int y = top;
    MoveWindow(g_hList, M, y, cw, listH, TRUE);           y += listH + ROW_GAP;
    MoveWindow(g_hRs, M, y, cw, 24, TRUE);                y += 24 + ROW_GAP;

    // 단축키 네 쌍을 한 줄에 늘어놓는다. 칸 하나가 (이름 + 콤보) 다.
    //
    // 2026-10-27. 이름 칸을 122 로 고정했더니 짧은 이름 뒤에 빈 자리가 남고
    // 정작 다음 이름과는 붙어 버렸다. 이름 길이를 글꼴로 재서 그만큼만 쓰고,
    // 콤보는 바로 붙이고, 남는 자리를 쌍과 쌍 사이에 고르게 나눈다.
    {
        const int GAP_NEAR = 6;    // 이름과 그 콤보 사이
        int lblW[HK_N], used = 0;
        HDC dc = GetDC(g_hWnd);
        HFONT old = (HFONT)SelectObject(dc, g_font);
        for (int k = 0; k < HK_N; k++) {
            wchar_t t[64];
            GetWindowTextW(g_hKeyLbl[k], t, 64);
            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
            lblW[k] = sz.cx + 2;
            used += lblW[k];
        }
        SelectObject(dc, old);
        ReleaseDC(g_hWnd, dc);

        // 콤보는 넷 다 같은 너비로. 남는 자리를 쌍 사이 간격으로 돌린다.
        int boxW = 92;
        int rest = cw - used - (GAP_NEAR + boxW) * HK_N;
        int farGap = (HK_N > 1) ? rest / (HK_N - 1) : 0;
        if (farGap < 10) {                       // 자리가 모자라면 콤보를 줄인다
            farGap = 10;
            boxW = (cw - used - GAP_NEAR * HK_N - farGap * (HK_N - 1)) / HK_N;
            if (boxW < 60) { boxW = 60; }
        }
        int x = M;
        for (int k = 0; k < HK_N; k++) {
            MoveWindow(g_hKeyLbl[k], x, y + 4, lblW[k], 20, TRUE);
            x += lblW[k] + GAP_NEAR;
            MoveWindow(g_hKeyBox[k], x, y, boxW, 260, TRUE);
            x += boxW + farGap;
        }
        y += KEY_H + ROW_GAP;
    }

    int bw = 170, bh = BTN_H, gap = 10;
    MoveWindow(g_hCopy,    M, y, 150, bh, TRUE);
    MoveWindow(g_hPick,    M + 150 + gap, y, 160, bh, TRUE);
    MoveWindow(g_hRestore, M + cw - 150, y, 150, bh, TRUE);
    MoveWindow(g_hInstall, M + cw - 150 - gap - bw, y, bw, bh, TRUE);
    y += bh + ROW_GAP;

    MoveWindow(g_hStatus, M, y, cw, 22, TRUE);            y += 22 + 8;
    // 맨 아래 한 줄을 판 번호에 내준다.
    const int VERLINE = 18;
    int logH2 = bottom - y - VERLINE - 4;
    if (logH2 < 80) { logH2 = 80; }
    MoveWindow(g_hLog, M, y, cw, logH2, TRUE);            y += logH2 + 4;
    MoveWindow(g_hVer, M, y, cw, VERLINE, TRUE);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if ((HWND)lp == g_hList && HIWORD(wp) == LBN_SELCHANGE) {
            SyncReShadeCheckboxToSelection();
            return 0;
        }
        // 단축키 칸을 직접 고르셨다. 이 뒤로는 게임을 바꿔도 이 칸을 안 건드린다.
        if (HIWORD(wp) == CBN_SELCHANGE) {
            for (int k = 0; k < HK_N; k++) {
                if ((HWND)lp == g_hKeyBox[k]) { g_hkTouched[k] = true; return 0; }
            }
        }
        if ((HWND)lp == g_hInstall)      StartJob(false);
        else if ((HWND)lp == g_hRestore) StartJob(true);
        else if ((HWND)lp == g_hCopy)    CopyLogToClipboard();
        else if ((HWND)lp == g_hPick)    PickGameFolder();
        return 0;
    case WM_SIZE:
        if (g_hLog) LayoutControls(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        // 2026-10-27. 720 으로 두었더니 단축키 네 칸이 겹쳤다.
        // 이름 122 + 콤보 60 + 사이 8 = 190, 네 칸이면 760.
        // 좌우 여백 40 과 테두리를 더해 820 이 최소다.
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 820;
        mmi->ptMinTrackSize.y = 600;
        return 0;
    }
    case WM_LOG:    { wchar_t* s = (wchar_t*)lp; AddLogLine(s); free(s); return 0; }
    case WM_STATUS: { wchar_t* s = (wchar_t*)lp; SetWindowTextW(g_hStatus, s); free(s); return 0; }
    case WM_DONE:
        g_busy = 0;
        EnableWindow(g_hInstall, TRUE); EnableWindow(g_hRestore, TRUE);
        EnableWindow(g_hPick, TRUE);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        if ((HWND)lp == g_hLog) {            // 읽기 전용 EDIT 은 배경을 칠해 줘야 스크롤 자국이 안 남는다
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    {   // 시험용: /gpu=amd, /gpu=rtx30 으로 카드를 흉내낸다
        std::wstring cl = LowerW(GetCommandLineW());
        size_t q = cl.find(L"/gpu=");
        if (q != std::wstring::npos) {
            size_t e = cl.find_first_of(L" \t", q);
            g_gpuFake = cl.substr(q + 5, (e == std::wstring::npos ? cl.size() : e) - (q + 5));
        }
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&ic);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = L"DLSS5SetupWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    std::wstring caption = std::wstring(L"DLSS 5 설치 도우미  ") + APP_VER;
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, caption.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 880, 720, NULL, NULL, hInst, NULL);

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
    g_fontTitle = CreateFontW(-23, 0, 0, 0, FW_BOLD, 0, 0, 0, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");

    g_hTitle = CreateWindowW(L"STATIC", L"DLSS 5 설치 도우미",
        WS_CHILD | WS_VISIBLE, 20, 14, 500, 32, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

    g_hDesc = CreateWindowW(L"STATIC",
        L"1) 게임을 고릅니다   2) [DLSS 5 설치]를 누릅니다   3) 게임에 DLSS 항목이 있으면 켜 두세요\n"
        L"찾는 곳 : 스팀 · 엑스박스 · GOG · 에픽 · 록스타 · 유비소프트.   없으면 [목록에 없는 게임]\n"
        L"되는 것 : DX11 · DX12 · 벌칸 게임.   안 되는 것 : DX9 · DX10 같은 오래된 게임\n"
        L"쓰지 말 것 : 온라인 게임. 안티치트가 dll 을 치트로 보고 계정을 정지시킵니다",
        WS_CHILD | WS_VISIBLE, 20, 50, 820, 78, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hDesc, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_hGpu = CreateWindowW(L"STATIC", L"그래픽카드를 확인하고 있습니다...",
        WS_CHILD | WS_VISIBLE, 20, 132, 820, 22, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hGpu, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20, 160, 820, 180, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_hCopy = CreateWindowW(L"BUTTON", L"진행 기록 복사",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 356, 150, 38, g_hWnd, NULL, hInst, NULL);
    g_hPick = CreateWindowW(L"BUTTON", L"목록에 없는 게임",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 180, 356, 160, 38, g_hWnd, NULL, hInst, NULL);
    g_hInstall = CreateWindowW(L"BUTTON", L"DLSS 5 설치",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 500, 356, 180, 38, g_hWnd, NULL, hInst, NULL);
    g_hRestore = CreateWindowW(L"BUTTON", L"되돌리기",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 690, 356, 150, 38, g_hWnd, NULL, hInst, NULL);
    // 2026-10-18. 리세이드를 넣을지 고를 수 있게 했다. 기본은 켜짐이다.
    // 리세이드 설치가 이 도구의 절반이라 끄는 것을 기본으로 두면 안 된다.
    // 리세이드가 d3d12.dll 자리에 들어가면 그쪽이 DXGI 팩토리를 만들어서,
    // DLSS 5 가 winmm.dll 자리일 때 Insert 창이 안 열리는 게임이 있다.
    // 효과 파일 886개를 읽느라 게임 설정 화면이 10초쯤 멈춘다는 제보도 있었다.
    // 필요한 분만 켜면 그 문제가 처음부터 안 생긴다.
    g_hRs = CreateWindowW(L"BUTTON", L"리세이드도 같이 설치 (끄면 DLSS 5 만 넣습니다)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 330, 500, 24, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hRs, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_hRs, WM_SETFONT, (WPARAM)g_font, TRUE);

    // 단축키 네 칸. 자리는 LayoutControls 가 잡는다. 여기서는 만들기만 한다.
    // 콤보 높이는 펼쳤을 때까지 합한 값이라 크게 준다. 접힌 높이는 글꼴이 정한다.
    for (int k = 0; k < HK_N; k++) {
        g_hKeyLbl[k] = CreateWindowW(L"STATIC", HK_LABEL[k],
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_hWnd, NULL, hInst, NULL);
        g_hKeyBox[k] = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
            0, 0, 10, 260, g_hWnd, NULL, hInst, NULL);
        SendMessageW(g_hKeyLbl[k], WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_hKeyBox[k], WM_SETFONT, (WPARAM)g_font, TRUE);
        for (int i = 0; i < KEY_COUNT; i++) {
            SendMessageW(g_hKeyBox[k], CB_ADDSTRING, 0, (LPARAM)KEY_LIST[i].name);
        }
        SendMessageW(g_hKeyBox[k], CB_SETCURSEL, (WPARAM)VkToRow(HK_DEFAULT[k]), 0);
    }

    SendMessageW(g_hCopy,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hPick,    WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hInstall, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_hRestore, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_hStatus = CreateWindowW(L"STATIC", L"준비됨",
        WS_CHILD | WS_VISIBLE, 20, 402, 820, 22, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        20, 428, 820, 232, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_font, TRUE);
    // EDIT 컨트롤은 한도를 안 정하면 32,767자에서 막힌다. 거기까지 차면
    // 그 뒤로는 한 글자도 안 들어간다. 앞이 밀려 나가는 게 아니라 뒤가 막힌다.
    // 게임을 여러 개 설치하면 그 한도에 닿는다. 2026-10-25 대표님이 찾으셨다.
    SendMessageW(g_hLog, EM_SETLIMITTEXT, (WPARAM)LOG_UI_MAX, 0);

    // 오른쪽 아래에 판 번호. 제목줄에도 있지만 작아서 확인이 어렵다.
    g_hVer = CreateWindowW(L"STATIC", APP_VER,
        WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10, 10, g_hWnd, NULL, hInst, NULL);
    SendMessageW(g_hVer, WM_SETFONT, (WPARAM)g_font, TRUE);

    {
        RECT rc; GetClientRect(g_hWnd, &rc);
        LayoutControls(rc.right, rc.bottom);
    }
    ShowWindow(g_hWnd, SW_SHOW);

    g_logPath = SelfDir() + L"\\" + LOG_FILE;
    WriteFileBytes(g_logPath, "\xEF\xBB\xBF");   // UTF-8 BOM

    AddLogLine((std::wstring(L"DLSS 5 설치 도우미 ") + APP_VER + L"   OptiScaler " + OPTI_VER).c_str());
    AddLogLine((L"이 기록은 " + g_logPath + L" 에도 저장됩니다. 문제가 생기면 [진행 기록 복사]를 눌러 게시물 댓글에 붙여넣어 주세요.").c_str());

    // ── 문제 보고용 환경 정보. 이게 없으면 원격으로 원인을 못 잡는다.
    {
        OSVERSIONINFOEXW vi = {}; vi.dwOSVersionInfoSize = sizeof(vi);
        typedef LONG (WINAPI *RtlGetVersionPtr)(OSVERSIONINFOEXW*);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionPtr rgv = nt ? (RtlGetVersionPtr)GetProcAddress(nt, "RtlGetVersion") : NULL;
        if (rgv) rgv(&vi);
        wchar_t b[400];
        swprintf_s(b, L"윈도우: %lu.%lu 빌드 %lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
        AddLogLine(b);
        AddLogLine((std::wstring(L"관리자 권한: ") + (IsElevated() ? L"예" : L"아니오")).c_str());
        AddLogLine((L"프로그램 위치: " + SelfDir()).c_str());
        swprintf_s(b, L"프로그램 폴더 여유 공간: %lld MB", FreeSpaceMB(SelfDir()));
        AddLogLine(b);
        AddLogLine(L"같이 들어 있어야 할 파일을 확인합니다...");
        std::wstring rep;
        int bad = CheckOwnFiles(rep);   // 신경망 모델 CRC 대조는 NEEDED[] 안에서 한다
        if (bad == 0) {
            AddLogLine(L"  → 모두 있습니다. 이상 없습니다.");
        } else {
            AddLogLine(L"  → 빠지거나 깨진 것이 있습니다:");
            AddLogLine(rep.c_str());
            std::wstring NLs(1, (wchar_t)10);
            std::wstring m;
            m += L"프로그램이 쓸 파일이 온전하지 않습니다." + NLs + NLs;
            m += rep + NLs;
            m += L"이대로 설치하면 게임에서 DLSS 5가 켜지지 않습니다." + NLs + NLs;
            m += L"받으신 압축을 다시 풀어 주세요." + NLs;
            m += L"예전에 받아 두신 폴더가 섞이지 않도록, 새 폴더에 푸시는 것이 안전합니다.";
            MessageBoxW(NULL, m.c_str(), L"파일이 온전하지 않습니다",
                        MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
    }
    AddLogLine(L"그래픽카드를 확인하고 있습니다...");
    GpuInfo gpu = DetectGpu();
    if (g_gpuFake == L"amd") {
        gpu.isNvidia = false; gpu.tier = 0; gpu.series = 0;
        gpu.name = L"AMD Radeon RX 7800 XT (시험용 흉내)";
    } else if (g_gpuFake == L"rtx30") {
        gpu.isNvidia = true; gpu.tier = 1; gpu.series = 3;
        gpu.name = L"NVIDIA GeForce RTX 3080 (시험용 흉내)";
    }
    g_tier = gpu.tier;
    g_gpuNvidia = gpu.isNvidia;
    g_gpuSeries = gpu.series;
    g_gpuName = gpu.name.empty() ? std::wstring(L"확인 불가") : gpu.name;
    std::wstring gline;
    if (gpu.isNvidia) {
        gline = L"그래픽카드: " + gpu.name;
        if (!gpu.nvVersion.empty()) gline += L"      드라이버 " + gpu.nvVersion;
        {
            wchar_t tb[160];
            swprintf_s(tb, L"      (이 판은 드라이버 %s 에서 시험했습니다)", TESTED_DRV);
            gline += tb;
        }
        SetWindowTextW(g_hGpu, gline.c_str());
        AddLogLine(gline.c_str());
        // 2026-10-18. 신경망 파일이 요구하는 드라이버를 확인한다.
        //   nvngx_dlssnr.dll 안에 NGXMinimumDriverVersion 615.00 이라고 적혀 있다.
        //   610.88 에서 프래그마타가 켜자마자 꺼졌다.
        // 게임마다 다르게 나타나므로 막지는 않고 알려만 준다.
        if (!gpu.nvVersion.empty()) {
            double v = _wtof(gpu.nvVersion.c_str());
            if (v > 0 && v < 615.0) {
                AddLogLine(L"[주의] 드라이버가 615.00 보다 낮습니다.");
                AddLogLine(L"  DLSS 5 신경망 파일은 615.00 이상을 요구합니다.");
                AddLogLine(L"  낮은 드라이버에서는 게임에 따라 켜자마자 꺼질 수 있습니다.");
                AddLogLine(L"  엔비디아 앱에서 드라이버를 올리신 뒤 쓰시는 것이 좋습니다.");
            }
        }

        if (gpu.tier == 2)
            AddLogLine(L"RTX 50 시리즈입니다.");
        else if (gpu.tier == 1 && gpu.series == 4)
            AddLogLine(L"RTX 40 시리즈입니다.");
        else if (gpu.tier == 1)
            AddLogLine(L"RTX 20 / 30 시리즈입니다. 이 시리즈에서는 확인하지 못했습니다.");
        else
            AddLogLine(L"RTX 20 시리즈보다 오래된 카드는 DLSS 5를 쓸 수 없습니다.");
    } else {
        gline = L"그래픽카드: " + (gpu.name.empty() ? std::wstring(L"확인 불가") : gpu.name) + L"   (엔비디아 카드가 아닙니다)";
        SetWindowTextW(g_hGpu, gline.c_str());
        AddLogLine(gline.c_str());
        AddLogLine(L"DLSS 5는 엔비디아 RTX 카드에서만 동작합니다.");
    }

    AddLogLine(L"설치된 게임을 찾고 있습니다...");
    ScanAll();
    for (size_t i = 0; i < g_games.size(); i++)
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)g_games[i].name.c_str());
    wchar_t b[128]; swprintf_s(b, L"게임 %d개를 찾았습니다. 목록에 없으면 [목록에 없는 게임] 을 쓰세요.", (int)g_games.size());
    AddLogLine(b);
    SyncReShadeCheckboxToSelection();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hWnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
