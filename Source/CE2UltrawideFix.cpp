// CE2UltrawideFix v2 - The Caligula Effect 2 (UE4.24) 32:9 ultrawide patch
//  1) AspectFix: clears FMinimalViewInfo.bConstrainAspectRatio copies (removes pillarbox)
//  2) FOVFix: forces ULocalPlayer.AspectRatioAxisConstraint = MaintainYFOV (Hor+ FOV)
// Proxy: X3DAudio1_7.dll next to TheCaligulaEffect2-Win64-Shipping.exe
#pragma comment(linker, "/export:X3DAudioCalculate=X3DAudio1_7_orig.X3DAudioCalculate")
#pragma comment(linker, "/export:X3DAudioInitialize=X3DAudio1_7_orig.X3DAudioInitialize")
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string>

static FILE* gLog = nullptr;
static void Log(const char* fmt, ...) {
    if (!gLog) {
        wchar_t modpath[MAX_PATH];
        GetModuleFileNameW(GetModuleHandleA(NULL), modpath, MAX_PATH);
        std::wstring dir(modpath);
        dir = dir.substr(0, dir.find_last_of(L"\\") + 1) + L"CE2UltrawideFix.log";
        gLog = _wfsopen(dir.c_str(), L"w", _SH_DENYNO);
    }
    if (!gLog) return;
    va_list ap; va_start(ap, fmt);
    char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(gLog, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    fflush(gLog);
}

static bool g_EnableAspectFix = true;
static bool g_EnableFovFix = true;

static void LoadConfig() {
    wchar_t modpath[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleA(NULL), modpath, MAX_PATH);
    std::wstring dir(modpath);
    dir = dir.substr(0, dir.find_last_of(L"\\") + 1) + L"CE2UltrawideFix.ini";
    FILE* f = _wfsopen(dir.c_str(), L"r", _SH_DENYNO);
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int v = -1;
        if (sscanf(line, "EnableAspectFix=%d", &v) == 1) g_EnableAspectFix = (v != 0);
        if (sscanf(line, "EnableFOVFix=%d", &v) == 1) g_EnableFovFix = (v != 0);
    }
    fclose(f);
    Log("Config: EnableAspectFix=%d EnableFOVFix=%d", g_EnableAspectFix, g_EnableFovFix);
}

static uint8_t* gBase = nullptr;
static size_t gSize = 0;

static void InitModuleInfo() {
    gBase = (uint8_t*)GetModuleHandleA(NULL);
    auto dos = (PIMAGE_DOS_HEADER)gBase;
    auto nt = (PIMAGE_NT_HEADERS)(gBase + dos->e_lfanew);
    gSize = nt->OptionalHeader.SizeOfImage;
}

// pattern scan; returns every hit through callback
static void PatternScanAll(const char* sig, void(*cb)(uint8_t*, void*), void* user) {
    int bytes[32]; int n = 0;
    const char* p = sig;
    while (*p && n < 32) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') { bytes[n++] = -1; p += 2; }
        else { bytes[n++] = (int)strtoul(p, const_cast<char**>(&p), 16); }
    }
    for (size_t i = 0; i + n <= gSize; i++) {
        bool ok = true;
        for (int j = 0; j < n; j++) if (bytes[j] != -1 && gBase[i + j] != (uint8_t)bytes[j]) { ok = false; break; }
        if (ok) cb(gBase + i, user);
    }
}

// ---------- 1) aspect pillarbox fix ----------
static void PatchAspectHit(uint8_t* hit, void*) {
    static const uint8_t repl[9] = { 0x80, 0x61, 0x30, 0xFE, 0x90, 0x90, 0x90, 0x90, 0x90 };
    uint8_t* at = hit + 3; // skip mov eax,[rcx+30]
    DWORD old;
    if (VirtualProtect(at, sizeof(repl), PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(at, repl, sizeof(repl));
        VirtualProtect(at, sizeof(repl), old, &old);
        Log("AspectFix: patched at exe+0x%llx", (unsigned long long)(at - gBase));
    } else {
        Log("AspectFix: VirtualProtect FAILED at exe+0x%llx", (unsigned long long)(at - gBase));
    }
}

static int g_aspectFound = 0;
static void CountAspectHit(uint8_t*, void*) { g_aspectFound++; }

static void DoAspectFix() {
    const char* sig = "8B 41 30 33 42 30 83 E0 01 31 41 30";
    g_aspectFound = 0;
    PatternScanAll(sig, CountAspectHit, nullptr);
    if (g_aspectFound == 0) { Log("AspectFix: pattern NOT found (game updated?)"); return; }
    PatternScanAll(sig, PatchAspectHit, nullptr);
    Log("AspectFix: %d site(s) patched", g_aspectFound);
}

// ---------- 2) FOV fix ----------
static uintptr_t gGEngine = 0;
static uintptr_t gGUObjectArray = 0;

// scan whole module once for sig, return first hit
static uint8_t* ScanOnce(const char* sig) {
    int bytes[32]; int n = 0;
    const char* p = sig;
    while (*p && n < 32) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') { bytes[n++] = -1; p += 2; }
        else { bytes[n++] = (int)strtoul(p, const_cast<char**>(&p), 16); }
    }
    for (size_t i = 0; i + n <= gSize; i++) {
        bool ok = true;
        for (int j = 0; j < n; j++) if (bytes[j] != -1 && gBase[i + j] != (uint8_t)bytes[j]) { ok = false; break; }
        if (ok) return gBase + i;
    }
    return nullptr;
}

static bool FindEngineGlobals() {
    // GEngine: 48 8B 05 ?? ?? ?? ?? 48 8B 88 C0 07 00 00 48 85 C9 74 ??
    uint8_t* hit = ScanOnce("48 8B 05 ?? ?? ?? ?? 48 8B 88 C0 07 00 00 48 85 C9 74 ??");
    if (hit) {
        int32_t disp = *(int32_t*)(hit + 3);
        uintptr_t glob = (uintptr_t)(hit + 7 + disp);
        gGEngine = *(uintptr_t*)glob;
        Log("FOV: GEngine global=exe+0x%llx GEngine=%p", (unsigned long long)(glob - (uintptr_t)gBase), (void*)gGEngine);
    } else {
        Log("FOV: GEngine pattern NOT found");
    }
    // GUObjectArray: 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C9 4C 89 74 24 20
    uint8_t* hit2 = ScanOnce("48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C9 4C 89 74 24 20");
    if (hit2) {
        int32_t disp = *(int32_t*)(hit2 + 3);
        gGUObjectArray = (uintptr_t)(hit2 + 7 + disp);
        Log("FOV: GUObjectArray=exe+0x%llx", (unsigned long long)(gGUObjectArray - (uintptr_t)gBase));
    } else {
        Log("FOV: GUObjectArray pattern NOT found");
    }
    return gGEngine != 0 && gGUObjectArray != 0;
}

static uintptr_t FindLocalPlayerByScan(uintptr_t vc) {
    // FUObjectArray (4.24): +0x00 NumElements(int32), +0x10 FUObjectChunk** Chunks; item stride 0x18
    int32_t num = 0;
    uintptr_t chunks = 0;
    __try {
        num = *(int32_t*)gGUObjectArray;
        chunks = *(uintptr_t*)(gGUObjectArray + 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (num <= 0 || num > 5000000 || chunks < 0x10000000000ULL || chunks > 0x7FFFFFFFFFFFULL) return 0;

    int scanned = 0;
    for (int ci = 0; ci < 2048; ci++) { // walk chunks until null
        uintptr_t chunk = 0;
        __try { chunk = *(uintptr_t*)(chunks + ci * 8); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (!chunk) break;
        for (int j = 0; j < 65536; j++) {
            uintptr_t obj = 0;
            __try { obj = *(uintptr_t*)(chunk + j * 0x18); } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
            if (obj < 0x10000000000ULL || obj > 0x7FFFFFFFFFFFULL) continue;
            scanned++;
            uintptr_t backlink = 0;
            __try { backlink = *(uintptr_t*)(obj + 0x70); } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
            if (backlink != vc) continue;
            uint8_t c = 0xFF;
            __try { c = *(uint8_t*)(obj + 0x94); } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
            if (c <= 2) {
                Log("FOV: LocalPlayer found at %p (chunk %d idx %d, scanned %d)", (void*)obj, ci, j, scanned);
                return obj;
            }
        }
    }
    Log("FOV: LP scan found nothing (scanned %d live of num=%d)", scanned, num);
    return 0;
}

static DWORD WINAPI FovThread(void*) {
    bool ok = false;
    for (int i = 0; i < 240 && !ok; i++) { Sleep(500); ok = FindEngineGlobals(); }
    if (!ok) { Log("FOV: engine globals unavailable, FOV fix DISABLED"); return 0; }

    uintptr_t lp = 0;
    int rescanCooldown = 0;
    for (;;) {
        Sleep(500);
        if (!g_EnableFovFix) continue;
        uintptr_t vc = 0;
        __try { vc = *(uintptr_t*)(gGEngine + 0x7c0); } __except (EXCEPTION_EXECUTE_HANDLER) { vc = 0; }
        if (!vc) { lp = 0; continue; }
        if (lp) {
            uintptr_t bl = 0;
            __try { bl = *(uintptr_t*)(lp + 0x70); } __except (EXCEPTION_EXECUTE_HANDLER) { bl = 0; }
            if (bl != vc) { lp = 0; rescanCooldown = 0; }
        }
        if (!lp) {
            if (rescanCooldown > 0) { rescanCooldown--; continue; }
            lp = FindLocalPlayerByScan(vc);
            if (!lp) { rescanCooldown = 8; continue; } // retry every ~4s
        }
        __try {
            uint8_t c = *(uint8_t*)(lp + 0x94);
            if (c != 0) {
                *(uint8_t*)(lp + 0x94) = 0;
                Log("FOV: MaintainYFOV enforced (LP=%p was %d)", (void*)lp, c);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { lp = 0; }
    }
    return 0;
}

static DWORD WINAPI MainThread(void*) {
    InitModuleInfo();
    LoadConfig();
    Log("=== CE2UltrawideFix v2 loaded (base=%p) ===", gBase);
    if (g_EnableAspectFix) DoAspectFix();
    if (g_EnableFovFix) CreateThread(NULL, 0, FovThread, NULL, 0, NULL);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
