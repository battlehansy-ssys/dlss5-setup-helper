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
static const wchar_t* APP_VER  = L"1.0.0.8";
static const wchar_t* OPTI_VER = L"v0.2.0-dlssnr";
static const wchar_t* OPTI_URL =
    L"https://github.com/Dagherbou/OptiScaler_DLSSNR/releases/download/v0.2.0-dlssnr/OptiScaler-DLSSNR-v0.2.0.zip";
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

// 설치할 때 ini에 써 넣는 값
struct IniSet { const wchar_t* section; const wchar_t* key; const wchar_t* value; };
static const IniSet PRESET[] = {
    // 켜는 데 꼭 필요한 것만 건드린다. 화질 관련 값은 OptiScaler 기본값 그대로 둔다.
    { L"Upscalers", L"Dx11Upscaler",   L"dlss_12" },
    { L"Upscalers", L"Dx12Upscaler",   L"dlss"    },
    { L"Upscalers", L"VulkanUpscaler", L"dlss"    },
    { L"DlssNr",    L"Enabled",        L"true"    },
    { L"DlssNr",    L"ToggleKey",      L"0x76"    },
};

// 게임이 읽어 들일 수 있는 이름들 (앞에서부터 시도)
static const wchar_t* PROXY_NAMES[] = {
    L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll", L"wininet.dll", L"winhttp.dll"
};

// ================================================================ 전역
static HWND g_hWnd, g_hList, g_hLog, g_hInstall, g_hRestore, g_hPick, g_hCopy, g_hStatus, g_hGpu, g_hApiLbl, g_hApi, g_hModeLbl, g_hMode;
static HWND g_hTitle, g_hDesc;
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

static void Log(const std::wstring& s)    { PostMessageW(g_hWnd, WM_LOG,    0, (LPARAM)_wcsdup(s.c_str())); }
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

static void CopyTree(const std::wstring& from, const std::wstring& to,
                     std::vector<std::wstring>& added, const std::wstring& rel = L"") {
    CreateDirectoryW(to.c_str(), NULL);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring src = from + L"\\" + n, dst = to + L"\\" + n;
        std::wstring r = rel.empty() ? n : rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyTree(src, dst, added, r);
        else { if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) added.push_back(r); }
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

static int CheckRevertLeftovers(const std::wstring& gameDir, std::wstring& reportOut) {
    std::wstring NL(1, (wchar_t)10);
    std::wstring bad;
    int miss = 0;

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
    };
    for (size_t i = 0; i < _countof(files); i++) {
        if (FileExists(gameDir + L"\\" + files[i]))
            { bad += std::wstring(L"  - ") + files[i] + NL; miss++; }
    }

    // 폴더
    const wchar_t* dirs[] = { L"OptiScaler", L"Licenses", L"_DLSS5_backup", L"host64" };
    for (int i = 0; i < 4; i++) {
        if (DirExists(gameDir + L"\\" + dirs[i]))
            { bad += std::wstring(L"  - ") + dirs[i] + L" 폴더" + NL; miss++; }
    }

    // 프록시로 쓰인 dll 이 아직 OptiScaler 인지
    const wchar_t* px[] = { L"winmm.dll", L"dxgi.dll", L"version.dll", L"dbghelp.dll",
                            L"wininet.dll", L"winhttp.dll" };
    for (int i = 0; i < 6; i++) {
        std::wstring p = gameDir + L"\\" + px[i];
        if (IsOptiScalerFile(p))
            { bad += std::wstring(L"  - ") + px[i] + L" (아직 OptiScaler 입니다)" + NL; miss++; }
    }

    // Feeder 셰이더
    if (FileExists(gameDir + L"\\reshade-shaders\\Shaders\\DLSS5_Feed.fx"))
        { bad += L"  - DLSS5_Feed.fx" + NL; miss++; }

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
    { L"data\\optiscaler.dat",                  130486024, L"OptiScaler 본체",      NULL },
    { L"data\\nvngx_dlssnr.dll",                165840496, L"신경망 모델",          L"9C56B352" },
    { L"data\\feeder\\nvngx_dlss.dll",          58956400, L"DLSS 런타임",          NULL },
    { L"data\\feeder\\dlss5-feed.addon64",        297472, L"DLSS5-Feeder",         NULL },
    { L"data\\feeder\\renodx-dlss5.addon64",     1732608, L"RenoDX 애드온",        NULL },
    { L"data\\feeder\\host64\\dlss5-feed-host64.exe", 146944, L"Feeder 보조 실행 파일", NULL },
    { L"data\\feeder\\shaders\\DLSS5_Feed.fx",          51193, L"Feeder 셰이더",    NULL },
    { L"data\\feeder\\dlss5-feed.cfg",               329, L"Feeder 설정",          NULL },
    { L"data\\feeder\\ReShadePreset.ini",            190, L"리세이드 프리셋",      NULL },
    { L"data\\reshade\\ReShade64.dll",           5592064, L"리세이드",             NULL },
    { L"data\\reshade\\ReShade.ini",                 316, L"리세이드 설정",        NULL },
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
static bool IsOptiScalerFile(const std::wstring& path) {
    return ProductNameOf(path).find(L"optiscaler") != std::wstring::npos;
}
static bool IsReShadeFile(const std::wstring& path) {
    return ProductNameOf(path).find(L"reshade") != std::wstring::npos;
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
            ln.find(L"vanguard") != std::wstring::npos ||
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
    const char* marks[] = { "NVSDK_NGX", "nvngx", "sl.interposer" };
    HANDLE h = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    std::vector<char> buf(1 << 20);
    std::string tail; DWORD got = 0; bool hit = false;
    while (!hit && ReadFile(h, &buf[0], (DWORD)buf.size(), &got, NULL) && got > 0) {
        std::string chunk = tail + std::string(&buf[0], got);
        for (int i = 0; i < 3 && !hit; i++)
            if (chunk.find(marks[i]) != std::string::npos) hit = true;
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

// 설치 방식 판단용. exe 가 크면 예전과 똑같이 exe 만 본다.
// exe 가 작으면 옆의 게임 본체 dll 에 DLSS 가 들어 있는지도 본다.
static int GameSupportsDlssFor(const std::wstring& exe) {
    // 폴더에 엔비디아 파일이 있으면 그걸로 끝이다. exe 안 글자보다 확실하다.
    // 껍데기 exe 를 쓰는 엔진(캡콤 RE 엔진은 _storage_ 안에 본체가 있다)이나
    // 데누보로 암호화된 exe 는 글자를 뒤져도 안 나온다. (프래그마타 사례)
    size_t cut = exe.find_last_of(L"\\");
    if (cut != std::wstring::npos) {
        std::wstring dir = exe.substr(0, cut);
        // 스트림라인 파일은 우리가 절대 넣지 않는다. 있으면 게임 것이다.
        const wchar_t* sure[] = { L"sl.interposer.dll", L"sl.dlss.dll" };
        for (size_t i = 0; i < _countof(sure); i++) {
            if (FileExists(dir + L"\\" + sure[i])) {
                Log(std::wstring(L"  ") + sure[i] + L" 이(가) 있습니다. DLSS 를 쓰는 게임입니다.");
                return 1;
            }
        }
        // nvngx_dlss.dll 은 ReShade 방식으로 설치할 때 우리가 직접 넣는다.
        // 그러니 우리가 넣은 흔적(RenoDX 애드온)이 없을 때만 증거로 쓴다.
        if (FileExists(dir + L"\\" + DLSS_DLL) && !FileExists(dir + L"\\" + RENODX_ADDON)) {
            Log(std::wstring(L"  ") + DLSS_DLL + L" 이(가) 있습니다. DLSS 를 쓰는 게임입니다.");
            return 1;
        }
    }
    int r = GameSupportsDlss(exe);
    if (r != 0) return r;
    if (FileSizeOf(exe) >= GAME_EXE_MIN) return 0;
    std::wstring eng = EngineDllNear(exe);
    return eng.empty() ? 0 : GameSupportsDlss(eng);
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
static std::wstring ChooseProxyName(const std::wstring& gameDir, bool& vulkanGuess) {
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
    if (vulkanGuess) order.push_back(L"winmm.dll");

    // 엔비디아 스트림라인을 쓰는 게임이면 dxgi.dll 을 피한다.
    // 스트림라인의 sl.interposer.dll 도 DXGI 를 가로채기 때문에
    // 같은 이름을 쓰면 후킹 시점이 어긋난다. OptiScaler 가 화면에
    // "Late Streamline hook detected" 를 띄우고, 게임에 따라
    // slInit() 이 0x18 로 실패하거나 내장 그래픽으로 붙어 터진다.
    if (FileExists(gameDir + L"\\sl.interposer.dll")) {
        Log(L"  엔비디아 스트림라인을 쓰는 게임입니다. dxgi.dll 대신 winmm.dll 로 넣습니다.");
        bool has = false;
        for (size_t j = 0; j < order.size(); j++) if (order[j] == L"winmm.dll") has = true;
        if (!has) order.push_back(L"winmm.dll");
        order.push_back(L"version.dll");
        order.push_back(L"dbghelp.dll");
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
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) out += lines[i];
    WriteFileBytes(iniPath, out);
    wchar_t b[128]; swprintf_s(b, L"  설정 %d개를 적용했습니다.", changed);
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

// ini 항목 하나만 바꾼다 (없는 항목은 만들지 않는다)
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
    if (t.find("DLSS5_Feed@DLSS5_Feed.fx") != std::string::npos) return true;

    // 기존 목록 앞에 우리 둘을 끼워 넣는다. 사용자가 켜 둔 효과는 유지한다.
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
            lines[i] = std::string(keys[k]) + WANT + (rest.empty() ? "" : "," + rest) + crlf;
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
// 깃허브에서 최신 릴리스의 zip 주소를 받아온다.
// 주소에 버전을 박아 두면 새 판이 나왔을 때 죽는다. 실패하면 박아 둔 주소로 돌아간다.
static std::wstring FetchLatestOptiUrl() {
    std::wstring tmp = TempDir() + L"release.json";
    DeleteFileW(tmp.c_str());
    const wchar_t* api =
        L"https://api.github.com/repos/Dagherbou/OptiScaler_DLSSNR/releases/latest";
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
        if (u.size() > 4 && u.compare(u.size() - 4, 4, ".zip") == 0)
            return U2W(u);
    }
    return L"";
}
// 신경망이 돌 해상도를 화면 크기에 맞춰 정한다.
// 고정값을 쓰면 4K 에서는 감당이 안 되고, 1440p 이하에서는 쓸데없이 흐려진다.
// 목표는 대략 260만 화소(=2172x1222 쯤) 언저리에서 도는 것이다.
static int PickWorkResolution(int& wOut, int& hOut) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
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
            Log(L"  ReShade.ini 는 이미 있어 건드리지 않습니다.");
        } else if (CopyFileW((rsSrc + L"\\" + RS_INI).c_str(), iniDst.c_str(), TRUE)) {
            added.push_back(RS_INI);
            Log(L"  ReShade.ini 를 넣었습니다. (Insert 키로 창이 열립니다)");
        }

        std::wstring shDst = gameDir + L"\\" + RS_SHADERS;
        if (HasFxFiles(shDst, 3)) {
            Log(L"  reshade-shaders 는 이미 있어 건드리지 않습니다.");
        } else if (DirExists(rsSrc + L"\\" + RS_SHADERS)) {
            if (DirExists(shDst))
                Log(L"  reshade-shaders 폴더는 있지만 효과 파일(.fx)이 없어 채워 넣습니다.");
            Status(L"효과 파일을 넣는 중입니다...");
            std::vector<std::wstring> got;
            CopyTree(rsSrc + L"\\" + RS_SHADERS, shDst, got);
            wchar_t b[160];
            swprintf_s(b, L"  reshade-shaders 를 넣었습니다. (파일 %d개)", (int)got.size());
            Log(b);
            added.push_back(RS_SHADERS);
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
        if (EnableFeedTechniques(preset)) {
            if (!existed) added.push_back(RS_PRESET);
            Log(L"  프리셋에서 모션 벡터와 DLSS5_Feed 효과를 켰습니다.");
            if (existed) Log(L"     쓰시던 효과는 그대로 두고 앞에 두 개만 추가했습니다.");
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
                Log(L"  껐다 켜는 키를 F7 로 맞췄습니다.");
        } else {
            Log(L"  껐다 켜는 키는 이미 정해져 있어 그대로 둡니다.");
        }

        // NRStyle 이 2 로 되어 있으면 다음 실행 때 게임이 안 켜진다.
        // 제작자 문서에 적힌 알려진 문제다. 0 으로 박아 둔다.
        std::string cur2 = ReadFileBytes(rsIni);
        if (cur2.find("NRStyle=2") != std::string::npos) {
            if (SetIniKey(rsIni, "RenoDX.DLSS5", "NRStyle", "0"))
                Log(L"  NR Style 이 2 로 되어 있어 0 으로 되돌렸습니다. (2 는 다음 실행 때 게임이 안 켜집니다)");
        } else if (cur2.find("NRStyle=") == std::string::npos) {
            SetIniKey(rsIni, "RenoDX.DLSS5", "NRStyle", "0");
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
            // 안의 파일만 지우면 빈 폴더가 남는다. 비어 있을 때만 지워진다.
            RemoveDirectoryW((gameDir + L"\\host64").c_str());
            RemoveTree(backup);
            // OptiScaler.ini 는 절대 여기서 지우지 않는다. 위 매니페스트 루프가
            // 백업본이 있으면 되살리고 없으면 지운다. 여기서 또 지우면 방금 되살린
            // 사용자의 원래 설정이 사라진다.
            const wchar_t* junk[] = { L"OptiScaler.log", L"setup_windows.bat",
                                      L"setup_linux.sh", L"READ ME - DLSS Neural Rendering.txt",
                                      L"!! EXTRACT ALL FILES TO GAME FOLDER !!" };
            for (int k = 0; k < 5; k++) DeleteFileW((gameDir + L"\\" + junk[k]).c_str());
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
                mr += L"되돌리기를 했지만 아직 남아 있는 것이 있습니다." + NLr + NLr;
                mr += left + NLr;
                mr += L"게임이나 다른 프로그램이 그 파일을 잡고 있을 수 있습니다." + NLr;
                mr += L"게임을 완전히 끄신 뒤 [되돌리기] 를 한 번 더 눌러 주세요." + NLr + NLr;
                mr += L"그래도 남으면 위 파일들을 직접 지우셔도 됩니다.";
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
        Log(L"OptiScaler " + std::wstring(OPTI_VER) + L" 를 내려받고 있습니다. 약 130MB, 인터넷 속도에 따라 몇 분 걸립니다.");
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
    const wchar_t* keep[] = { L"OptiScaler.ini", L"nvngx_dlss.dll" };
    for (int i = 0; i < 3; i++) {
        std::wstring name = (i < 2) ? std::wstring(keep[i]) : proxy;
        std::wstring from = gameDir + L"\\" + name;
        std::wstring to   = backup  + L"\\" + name;
        if (!FileExists(from)) continue;
        if (FileExists(to)) { Log(L"  이미 백업돼 있어 그대로 둡니다: " + name); continue; }
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
    CopyTree(srcRoot, gameDir, added);
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

    Log(L"DLSS 5를 켜는 설정만 넣고 있습니다. 화질 값은 제작자 기본값 그대로 둡니다.");
    ApplyPreset(gameDir + L"\\OptiScaler.ini");

    // 리세이드가 먼저 깔려 있으면 리세이드가 DXGI 팩토리를 감싸 버려서
    // OptiScaler 의 훅이 실패한다(로그에 Failed to hook IDXGIFactory / DLSS: 10DD,
    // 그 뒤로 초당 미니덤프). 이때는 훅 대신 감싸기로 들어가야 한다.
    // 2026-09-16 위처3(DX12 + 리세이드 6.7.3 dxgi.dll)에서 확인.
    if (HasReShade(gameDir)) {
        Log(L"  리세이드가 깔려 있습니다. 충돌을 피하려고 DxgiFactoryWrapping 을 켭니다.");
        SetIniKey(gameDir + L"\\OptiScaler.ini", "Spoofing", "DxgiFactoryWrapping", "true");
    }

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
        Log(L"  3. Insert 키로 리세이드 창을 열고 위쪽 RenoDX-DLSSNR 칸을 누르세요.");
        Log(L"     ACTIVE - NR INJECTED 라고 적혀 있으면 켜진 것입니다.");
        Log(L"  4. F7 키로 껐다 켜며 비교해 보세요. 얼굴 클로즈업에서 차이가 가장 잘 보입니다.");
    } else {
        Log(L"  1. 게임을 켭니다.");
        Log(L"  2. 게임 설정 → 그래픽 → 업스케일링(Upscaling)을 반드시 DLSS로 바꿉니다.");
        Log(L"     이걸 안 하면 DLSS 5는 켜지지 않습니다. FSR 이나 꺼짐으로 두면 안 됩니다.");
        Log(L"  3. 세이브를 불러와 실제 게임 화면으로 들어가세요. 메뉴 화면에서는 적용되지 않습니다.");
        Log(L"  4. Insert 키로 조절 창을 열고 맨 아래 DLSS Neural Rendering 항목을 펼치세요.");
        Log(L"  5. F7 키로 껐다 켜며 비교해 보세요. 얼굴 클로즈업에서 차이가 가장 잘 보입니다.");
        Log(L"");
        Log(L"※ 프레임이 모자라면 Insert 창에서 Model resolution 을 0.7 로 내리세요.");
    }
    Log(L"※ 피부가 하얗게 뜨거나 반짝이면 Detail 과 Colour 를 내리세요.");
    Log(L"※ 설치 직후 첫 실행은 실패할 수 있습니다. 그냥 한 번 더 켜시면 됩니다.");
    Log(L"   엔비디아가 뒤에서 파일을 정리하느라 그런 것이라 고장이 아닙니다.");
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
    int fixedBlock = 24 + ROW_GAP + BTN_H + ROW_GAP + 22 + 8;   // 체크박스 + 버튼줄 + 상태줄
    int space = bottom - top - fixedBlock;
    if (space < 200) space = 200;
    int listH = (int)(space * 0.40);
    if (listH < 100) listH = 100;
    int logH = space - listH;
    if (logH < 100) logH = 100;

    int y = top;
    MoveWindow(g_hList, M, y, cw, listH, TRUE);           y += listH + ROW_GAP;


    int bw = 170, bh = BTN_H, gap = 10;
    MoveWindow(g_hCopy,    M, y, 150, bh, TRUE);
    MoveWindow(g_hPick,    M + 150 + gap, y, 160, bh, TRUE);
    MoveWindow(g_hRestore, M + cw - 150, y, 150, bh, TRUE);
    MoveWindow(g_hInstall, M + cw - 150 - gap - bw, y, bw, bh, TRUE);
    y += bh + ROW_GAP;

    MoveWindow(g_hStatus, M, y, cw, 22, TRUE);            y += 22 + 8;
    MoveWindow(g_hLog,    M, y, cw, bottom - y, TRUE);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if ((HWND)lp == g_hInstall)      StartJob(false);
        else if ((HWND)lp == g_hRestore) StartJob(true);
        else if ((HWND)lp == g_hCopy)    CopyLogToClipboard();
        else if ((HWND)lp == g_hPick)    PickGameFolder();
        return 0;
    case WM_SIZE:
        if (g_hLog) LayoutControls(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 720;
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
        L"찾는 곳 : 스팀 · 엑스박스 · GOG · 에픽 · 록스타 · 유비소프트.   없으면 [폴더에서 고르기]\n"
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
    g_hPick = CreateWindowW(L"BUTTON", L"폴더에서 고르기",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 180, 356, 160, 38, g_hWnd, NULL, hInst, NULL);
    g_hInstall = CreateWindowW(L"BUTTON", L"DLSS 5 설치",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 500, 356, 180, 38, g_hWnd, NULL, hInst, NULL);
    g_hRestore = CreateWindowW(L"BUTTON", L"되돌리기",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 690, 356, 150, 38, g_hWnd, NULL, hInst, NULL);
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
        SetWindowTextW(g_hGpu, gline.c_str());
        AddLogLine(gline.c_str());
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
    wchar_t b[128]; swprintf_s(b, L"게임 %d개를 찾았습니다. 목록에 없으면 [폴더에서 고르기] 를 쓰세요.", (int)g_games.size());
    AddLogLine(b);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hWnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
