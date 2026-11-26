// gagfix - Windows 10/11 compatibility layer for "GAG: Отвязное приключение" (ZES't Corp., 1997)
//
// Built as gagfx.dll. The patched game executable imports it in place of WINMM.dll
// (the import name is renamed to GAGFX.dll). The seven winmm functions the game uses
// are forwarded to the real winmm (see the pragmas below).
//
// What it does:
//  * Makes the game believe the desktop runs in 16-bit HI-color, so it keeps its
//    native 16-bit (RGB565) GDI renderer. No real display mode change ever happens.
//  * Everything the game draws into its 640x480 picture window is captured into a
//    back buffer and presented scaled (4:3 aspect, integer or stretched, nearest or
//    smooth filter) once per display refresh by a presenter thread.
//  * Borderless fullscreen on the current monitor, or a resizable scaled window.
//    Alt+Enter switches between the two at any time.
//  * Mouse coordinates are mapped back to the game's 640x480 space.
//  * The process is per-monitor DPI-aware, so Windows does not blur the output again.

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>

#pragma comment(linker, "/export:waveOutClose=winmm.waveOutClose")
#pragma comment(linker, "/export:waveOutUnprepareHeader=winmm.waveOutUnprepareHeader")
#pragma comment(linker, "/export:waveOutReset=winmm.waveOutReset")
#pragma comment(linker, "/export:waveOutOpen=winmm.waveOutOpen")
#pragma comment(linker, "/export:waveOutWrite=winmm.waveOutWrite")
#pragma comment(linker, "/export:waveOutPrepareHeader=winmm.waveOutPrepareHeader")
#pragma comment(linker, "/export:timeGetTime=winmm.timeGetTime")

// =======================================================================================
// configuration (gagfix.ini next to the game exe)
// =======================================================================================
enum { MODE_FULLSCREEN = 0, MODE_WINDOWED = 1 };
enum { ASPECT_KEEP = 0, ASPECT_STRETCH = 1, ASPECT_INTEGER = 2 };

static struct {
    int mode;         // MODE_FULLSCREEN / MODE_WINDOWED at startup
    int scale;        // window scale for windowed mode, 0 = largest integer that fits
    int smooth;       // 0 = nearest neighbour (sharp pixels), 1 = smooth (halftone)
    int aspect;       // ASPECT_*
    int altenter;     // Alt+Enter toggles fullscreen/windowed
    int log;
} cfg;

static char  g_dir[MAX_PATH];
static char  g_ini[MAX_PATH];
static FILE* g_log;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void LoadConfig()
{
    char buf[64];
    GetPrivateProfileStringA("display", "mode", "fullscreen", buf, sizeof buf, g_ini);
    cfg.mode = (!lstrcmpiA(buf, "windowed") || !lstrcmpiA(buf, "window")) ? MODE_WINDOWED : MODE_FULLSCREEN;

    cfg.scale = GetPrivateProfileIntA("display", "window_scale", 0, g_ini);
    if (cfg.scale < 0) cfg.scale = 0;

    GetPrivateProfileStringA("display", "filter", "nearest", buf, sizeof buf, g_ini);
    cfg.smooth = !lstrcmpiA(buf, "smooth") || !lstrcmpiA(buf, "linear") || !lstrcmpiA(buf, "bilinear");

    GetPrivateProfileStringA("display", "aspect", "keep", buf, sizeof buf, g_ini);
    cfg.aspect = !lstrcmpiA(buf, "stretch") ? ASPECT_STRETCH : !lstrcmpiA(buf, "integer") ? ASPECT_INTEGER : ASPECT_KEEP;

    cfg.altenter = GetPrivateProfileIntA("display", "alt_enter", 1, g_ini);
    cfg.log = GetPrivateProfileIntA("debug", "log", 0, g_ini);
}

// =======================================================================================
// IAT patching
// =======================================================================================
static bool PatchIAT(HMODULE mod, const char* dll, const char* name, void* hook, void** orig)
{
    BYTE* base = (BYTE*)mod;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    bool done = false;
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (lstrcmpiA((char*)(base + imp->Name), dll)) continue;
        IMAGE_THUNK_DATA* nameThunk = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA* addrThunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; nameThunk->u1.AddressOfData; nameThunk++, addrThunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + nameThunk->u1.AddressOfData);
            if (lstrcmpA((char*)ibn->Name, name)) continue;
            DWORD old;
            VirtualProtect(&addrThunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            if (orig && !*orig) *orig = (void*)addrThunk->u1.Function;
            addrThunk->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&addrThunk->u1.Function, sizeof(void*), old, &old);
            done = true;
        }
    }
    return done;
}

// =======================================================================================
// original functions
// =======================================================================================
#define DECL(ret, name, args) typedef ret (WINAPI* name##_t) args; static name##_t o##name;
DECL(int,     GetDeviceCaps, (HDC, int))
DECL(BOOL,    EnumDisplaySettingsA, (LPCSTR, DWORD, DEVMODEA*))
DECL(LONG,    ChangeDisplaySettingsA, (DEVMODEA*, DWORD))
DECL(HWND,    CreateWindowExA, (DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID))
DECL(BOOL,    SetWindowPos, (HWND, HWND, int, int, int, int, UINT))
DECL(BOOL,    ShowWindow, (HWND, int))
DECL(BOOL,    GetClientRect, (HWND, LPRECT))
DECL(BOOL,    ScreenToClient, (HWND, LPPOINT))
DECL(BOOL,    BitBlt, (HDC, int, int, int, int, HDC, int, int, DWORD))
DECL(BOOL,    StretchBlt, (HDC, int, int, int, int, HDC, int, int, int, int, DWORD))
DECL(BOOL,    PatBlt, (HDC, int, int, int, int, DWORD))
DECL(int,     GetSystemMetrics, (int))
DECL(BOOL,    GetMessageA, (LPMSG, HWND, UINT, UINT))
DECL(LONG,    RegOpenKeyExA, (HKEY, LPCSTR, DWORD, REGSAM, PHKEY))
DECL(LONG,    RegQueryValueExA, (HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD))
DECL(LONG,    RegSetValueExA, (HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD))
DECL(LONG,    RegCloseKey, (HKEY))
DECL(void,    PostQuitMessage, (int))
DECL(BOOL,    PostMessageA, (HWND, UINT, WPARAM, LPARAM))
DECL(LRESULT, SendMessageA, (HWND, UINT, WPARAM, LPARAM))
DECL(BOOL,    PeekMessageA, (LPMSG, HWND, UINT, UINT, UINT))
#undef DECL

// =======================================================================================
// state
// =======================================================================================
static HWND  g_top;                 // FlcAppClassNT - the game's main window
static HWND  g_view;                // "Graphical System Child" - the game picture
static int   g_logW = 640, g_logH = 480;   // game picture size
static POINT g_gamePos;             // where the game thinks its picture sits in the main window
static int   g_mode;                // current MODE_*
static RECT  g_winRect;             // last windowed-mode window rect (for Alt+Enter)
static bool  g_haveWinRect;
static bool  g_inLayout;
static bool  g_gameFS;              // the game's own idea of "full screen" (its options button)
static bool  g_closing;
static int   g_inDeactivate;
static bool  g_wasIconic;
#define WM_APP_RELAYOUT (WM_APP + 0x47A)        // inside the game's own deactivation handling             // game is shutting down: stop following its mode calls

static CRITICAL_SECTION g_cs;
static HDC     g_bbDC;              // back buffer: what the game drew, at 640x480
static HBITMAP g_bbBmp, g_bbOld;
static volatile LONG g_dirty = 1;
static RECT    g_dest;              // where the picture goes inside g_view (physical px)
static SIZE    g_viewSize;          // g_view client size (physical px)

static WNDPROC g_viewProc, g_topProc;

// =======================================================================================
// geometry
// =======================================================================================
static void ComputeDest(int cw, int ch)
{
    int dw = cw, dh = ch;
    if (cfg.aspect != ASPECT_STRETCH && cw > 0 && ch > 0) {
        if (cfg.aspect == ASPECT_INTEGER) {
            int s = min(cw / g_logW, ch / g_logH);
            if (s >= 1) { dw = g_logW * s; dh = g_logH * s; }
            else goto keep;
        } else {
        keep:
            if ((long long)cw * g_logH > (long long)ch * g_logW) { dh = ch; dw = (int)((long long)ch * g_logW / g_logH); }
            else { dw = cw; dh = (int)((long long)cw * g_logH / g_logW); }
        }
    }
    EnterCriticalSection(&g_cs);
    g_viewSize.cx = cw; g_viewSize.cy = ch;
    g_dest.left = (cw - dw) / 2; g_dest.top = (ch - dh) / 2;
    g_dest.right = g_dest.left + dw; g_dest.bottom = g_dest.top + dh;
    g_dirty = 2;    // 2 = also repaint the borders
    LeaveCriticalSection(&g_cs);
}

// physical g_view client point -> game coordinates
static void ViewToGame(LONG& x, LONG& y)
{
    int dw = g_dest.right - g_dest.left, dh = g_dest.bottom - g_dest.top;
    if (dw <= 0 || dh <= 0) return;
    long gx = (long)(((long long)(x - g_dest.left) * g_logW) / dw);
    long gy = (long)(((long long)(y - g_dest.top) * g_logH) / dh);
    x = max(0L, min((long)g_logW - 1, gx));
    y = max(0L, min((long)g_logH - 1, gy));
}

static RECT MonitorRectOf(HWND h, bool work)
{
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    return work ? mi.rcWork : mi.rcMonitor;
}

// Size the main window for the current mode and make the picture fill its client area.
static void ApplyLayout()
{
    if (!g_top || g_inLayout) return;
    g_inLayout = true;

    LONG style = GetWindowLongA(g_top, GWL_STYLE);
    LONG ex    = GetWindowLongA(g_top, GWL_EXSTYLE);
    BOOL menu  = GetMenu(g_top) != NULL;
    RECT r;

    if (g_mode == MODE_FULLSCREEN) {
        style = (style & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE)) | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        ex &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
        SetWindowLongA(g_top, GWL_STYLE, style);
        SetWindowLongA(g_top, GWL_EXSTYLE, ex);
        r = MonitorRectOf(g_top, false);
        // keep a menu bar (if the game has one) just above the screen, like the game itself does
        AdjustWindowRectEx(&r, style, menu, ex);
    } else {
        style = (style & ~(WS_POPUP | WS_MAXIMIZE | WS_MINIMIZE)) | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        SetWindowLongA(g_top, GWL_STYLE, style);
        if (g_haveWinRect) {
            r = g_winRect;
        } else {
            RECT work = MonitorRectOf(g_top, true);
            int ww = work.right - work.left, wh = work.bottom - work.top;
            RECT probe = { 0, 0, 0, 0 };
            AdjustWindowRectEx(&probe, style, menu, ex);
            int ncw = probe.right - probe.left, nch = probe.bottom - probe.top;
            int s = cfg.scale;
            if (s <= 0) {
                s = min((ww - ncw) / g_logW, (wh - nch) / g_logH);
                if (s > 1 && (g_logW * s + ncw > ww * 95 / 100 || g_logH * s + nch > wh * 95 / 100)) s--;  // leave some room
                if (s < 1) s = 1;
            }
            int cw = g_logW * s, ch = g_logH * s;
            r.left = work.left + (ww - cw - ncw) / 2 - probe.left;
            r.top  = work.top + max(0, (wh - ch - nch) / 2) - probe.top;
            r.right = r.left + cw; r.bottom = r.top + ch;
            AdjustWindowRectEx(&r, style, menu, ex);
        }
    }
    oSetWindowPos(g_top, g_mode == MODE_FULLSCREEN ? HWND_TOP : HWND_NOTOPMOST, r.left, r.top,
                  r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (g_view) {
        RECT c; oGetClientRect(g_top, &c);
        oSetWindowPos(g_view, NULL, 0, 0, c.right, c.bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        ComputeDest(c.right, c.bottom);
        InvalidateRect(g_view, NULL, FALSE);
    }
    g_inLayout = false;
    Log("layout: mode=%d window=%d,%d %dx%d dest=%d,%d %dx%d", g_mode, r.left, r.top, r.right - r.left, r.bottom - r.top,
        g_dest.left, g_dest.top, g_dest.right - g_dest.left, g_dest.bottom - g_dest.top);
}

static void SaveMode()
{
    if (g_closing) return;
    WritePrivateProfileStringA("display", "mode", g_mode == MODE_FULLSCREEN ? "fullscreen" : "windowed", g_ini);
}

static void SetMode(int mode)
{
    if (mode == g_mode) return;
    if (g_mode == MODE_WINDOWED && g_top) { GetWindowRect(g_top, &g_winRect); g_haveWinRect = true; }
    g_mode = mode;
    ApplyLayout();
    SaveMode();
}

static void ToggleMode()
{
    SetMode(g_mode == MODE_FULLSCREEN ? MODE_WINDOWED : MODE_FULLSCREEN);
}


// =======================================================================================
// presenting
// =======================================================================================
static void EnsureBackBuffer()
{
    if (g_bbDC) return;
    HDC screen = GetDC(NULL);
    g_bbDC = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g_logW; bi.bmiHeader.biHeight = -g_logH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits;
    g_bbBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    g_bbOld = (HBITMAP)SelectObject(g_bbDC, g_bbBmp);
    oPatBlt(g_bbDC, 0, 0, g_logW, g_logH, BLACKNESS);
    ReleaseDC(NULL, screen);
}

static void Present(HDC dc)
{
    EnterCriticalSection(&g_cs);
    LONG d = InterlockedExchange(&g_dirty, 0);
    RECT dst = g_dest; SIZE vs = g_viewSize;
    (void)d;
    {   // black borders around the picture (repainted every time: cheap, and survives resizes)
        HBRUSH b = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RECT r;
        SetRect(&r, 0, 0, vs.cx, dst.top);           FillRect(dc, &r, b);
        SetRect(&r, 0, dst.bottom, vs.cx, vs.cy);     FillRect(dc, &r, b);
        SetRect(&r, 0, dst.top, dst.left, dst.bottom); FillRect(dc, &r, b);
        SetRect(&r, dst.right, dst.top, vs.cx, dst.bottom); FillRect(dc, &r, b);
    }
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (dw == g_logW && dh == g_logH) {
        oBitBlt(dc, dst.left, dst.top, dw, dh, g_bbDC, 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(dc, cfg.smooth ? HALFTONE : COLORONCOLOR);
        if (cfg.smooth) SetBrushOrgEx(dc, 0, 0, NULL);
        oStretchBlt(dc, dst.left, dst.top, dw, dh, g_bbDC, 0, 0, g_logW, g_logH, SRCCOPY);
    }
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI PresenterThread(LPVOID)
{
    typedef HRESULT (WINAPI* DwmFlush_t)();
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    DwmFlush_t pDwmFlush = dwm ? (DwmFlush_t)GetProcAddress(dwm, "DwmFlush") : NULL;
    timeBeginPeriod(1);
    for (;;) {
        if (!pDwmFlush || FAILED(pDwmFlush())) Sleep(8);   // wait for the next compositor frame
        HWND v = g_view;
        if (!v || !g_dirty || !IsWindow(v) || IsIconic(g_top)) continue;
        HDC dc = GetDC(v);
        if (dc) { Present(dc); ReleaseDC(v, dc); }
    }
}

static bool IsViewDC(HDC dc)
{
    return g_view && dc && WindowFromDC(dc) == g_view;
}

static BOOL WINAPI hBitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop)
{
    if (!IsViewDC(dst)) return oBitBlt(dst, x, y, w, h, src, sx, sy, rop);
    EnterCriticalSection(&g_cs);
    EnsureBackBuffer();
    BOOL r = oBitBlt(g_bbDC, x, y, w, h, src, sx, sy, rop);
    if (!g_dirty) g_dirty = 1;
    LeaveCriticalSection(&g_cs);
    return r;
}

static BOOL WINAPI hStretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop)
{
    if (!IsViewDC(dst)) return oStretchBlt(dst, x, y, w, h, src, sx, sy, sw, sh, rop);
    EnterCriticalSection(&g_cs);
    EnsureBackBuffer();
    SetStretchBltMode(g_bbDC, COLORONCOLOR);
    BOOL r = oStretchBlt(g_bbDC, x, y, w, h, src, sx, sy, sw, sh, rop);
    if (!g_dirty) g_dirty = 1;
    LeaveCriticalSection(&g_cs);
    return r;
}

static BOOL WINAPI hPatBlt(HDC dst, int x, int y, int w, int h, DWORD rop)
{
    if (!IsViewDC(dst)) return oPatBlt(dst, x, y, w, h, rop);
    EnterCriticalSection(&g_cs);
    EnsureBackBuffer();
    BOOL r = oPatBlt(g_bbDC, x, y, w, h, rop);
    if (!g_dirty) g_dirty = 1;
    LeaveCriticalSection(&g_cs);
    return r;
}

// =======================================================================================
// window procedures
// =======================================================================================
// The game hides the system cursor (it draws its own). Over the window frame and title
// bar in windowed mode the normal arrow must be visible, so raise the show count there.
static int g_cursorBoost;
static void NonClientCursor(bool show)
{
    if (show) {
        if (g_cursorBoost) return;
        int c;
        do { c = ShowCursor(TRUE); g_cursorBoost++; } while (c < 0 && g_cursorBoost < 64);
    } else {
        while (g_cursorBoost > 0) { ShowCursor(FALSE); g_cursorBoost--; }
    }
}

static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_SETCURSOR:
        NonClientCursor(false);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        EnterCriticalSection(&g_cs); EnsureBackBuffer(); g_dirty = 2; LeaveCriticalSection(&g_cs);
        Present(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: {
        LONG x = (short)LOWORD(l), y = (short)HIWORD(l);
        ViewToGame(x, y);
        l = MAKELPARAM(x, y);
        break;
    }
    case WM_SIZE:
        ComputeDest(LOWORD(l), HIWORD(l));
        break;
    }
    return CallWindowProcA(g_viewProc, h, m, w, l);
}

// Alt+Enter, whichever game window has the keyboard focus
static bool FilterMessage(MSG* m)
{
    if (!cfg.altenter || !g_top) return false;
    if (m->message == WM_SYSKEYDOWN && m->wParam == VK_RETURN && (m->lParam & (1 << 29))) {
        if (!(m->lParam & (1 << 30))) ToggleMode();
        m->message = WM_NULL;
        return true;
    }
    if ((m->message == WM_SYSCHAR || m->message == WM_SYSKEYUP) && m->wParam == VK_RETURN) { m->message = WM_NULL; return true; }
    return false;
}

static BOOL WINAPI hGetMessageA(LPMSG m, HWND h, UINT a, UINT b)
{
    BOOL r = oGetMessageA(m, h, a, b);
    if (r > 0) FilterMessage(m);
    return r;
}

static BOOL WINAPI hPeekMessageA(LPMSG m, HWND h, UINT a, UINT b, UINT rm)
{
    BOOL r = oPeekMessageA(m, h, a, b, rm);
    if (r && (rm & PM_REMOVE)) FilterMessage(m);
    return r;
}

static LRESULT CALLBACK TopProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_SETCURSOR:
        if ((HWND)w == h) NonClientCursor(LOWORD(l) != HTCLIENT);
        break;
    case WM_ACTIVATEAPP:
    case WM_ACTIVATE:
        // On losing focus the game minimizes itself from inside its deactivation handler
        // (the old "disappears into the taskbar" problem). Mark that window of time.
        if ((m == WM_ACTIVATEAPP && !w) || (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE)) {
            g_inDeactivate++;
            LRESULT r = CallWindowProcA(g_topProc, h, m, w, l);
            g_inDeactivate--;
            return r;
        }
        break;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MINIMIZE) {
            if (g_inDeactivate && !g_closing) { Log("ignored self-minimize (nested)"); return 0; }
            Log("minimize (user)");
            if (g_mode == MODE_WINDOWED && !IsZoomed(h) && !IsIconic(h)) { GetWindowRect(h, &g_winRect); g_haveWinRect = true; }
        }
        break;
    case WM_APP_RELAYOUT:
        if (!IsIconic(h) && !(g_mode == MODE_WINDOWED && IsZoomed(h))) ApplyLayout();
        return 0;
    case WM_SYSKEYDOWN:
        if (cfg.altenter && w == VK_RETURN && (l & (1 << 29)) && !(l & (1 << 30))) { ToggleMode(); return 0; }
        break;
    case WM_SYSCHAR:
        if (cfg.altenter && w == VK_RETURN) return 0;   // no beep
        break;
    case WM_SIZE:
        // coming back from the taskbar: Windows restores the game's original creation
        // rectangle, so put our own layout back once the restore has finished
        if (w == SIZE_RESTORED && g_wasIconic) PostMessageA(h, WM_APP_RELAYOUT, 0, 0);
        g_wasIconic = w == SIZE_MINIMIZED;
        if (g_view && w != SIZE_MINIMIZED && !g_inLayout) {
            oSetWindowPos(g_view, NULL, 0, 0, LOWORD(l), HIWORD(l), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
            ComputeDest(LOWORD(l), HIWORD(l));
            InvalidateRect(g_view, NULL, FALSE);
        }
        break;
    case WM_CLOSE: case WM_DESTROY: case WM_ENDSESSION:
        Log("main window msg %x", m);
        if (m != WM_ENDSESSION || w) g_closing = true;
        break;
    case WM_EXITSIZEMOVE:
        if (g_mode == MODE_WINDOWED) { GetWindowRect(h, &g_winRect); g_haveWinRect = true; }
        break;
    case WM_DPICHANGED:
        if (g_mode == MODE_FULLSCREEN) { LRESULT r = CallWindowProcA(g_topProc, h, m, w, l); ApplyLayout(); return r; }
        break;
    case WM_ERASEBKGND: {
        RECT c; oGetClientRect(h, &c);
        FillRect((HDC)w, &c, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    }
    return CallWindowProcA(g_topProc, h, m, w, l);
}

// =======================================================================================
// fake 16-bit display
// =======================================================================================
static int WINAPI hGetDeviceCaps(HDC dc, int index)
{
    int r = oGetDeviceCaps(dc, index);
    if (index == BITSPIXEL && r > 16) r = 16;
    return r;
}

static BOOL WINAPI hEnumDisplaySettingsA(LPCSTR dev, DWORD mode, DEVMODEA* dm)
{
    BOOL r = oEnumDisplaySettingsA(dev, mode, dm);
    if (r && dm->dmBitsPerPel > 16) dm->dmBitsPerPel = 16;
    return r;
}

// The game's own "Full Screen / Window" option switches the real display mode.
// We never do that; instead its request picks our borderless fullscreen or the window.
static bool GameIsForeground()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static LONG WINAPI hChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags)
{
    if (flags & CDS_TEST) return DISP_CHANGE_SUCCESSFUL;
    bool wantFS = dm != NULL;
    Log("ChangeDisplaySettings %s (faked)%s", wantFS ? "set mode" : "restore", g_closing ? " [closing]" : "");
    if (wantFS != g_gameFS && g_top && !g_closing && GameIsForeground()) {
        g_gameFS = wantFS;
        SetMode(wantFS ? MODE_FULLSCREEN : MODE_WINDOWED);
    }
    return DISP_CHANGE_SUCCESSFUL;
}

// =======================================================================================
// registry: HKLM\SOFTWARE\ZES't Corp.\GAG is served from gagfix.ini, so the game needs
// no installer entry and can save its options without administrator rights
// =======================================================================================
#define FAKE_KEY ((HKEY)(ULONG_PTR)0x6A6A6A01)
static const char kGameKey[] = "SOFTWARE\\ZES't Corp.\\GAG";
static const char kVersion[] = "Russian Edition Version 2.51";
enum { SET_FULLSCREEN = 0x20 };

static LONG WINAPI hRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out)
{
    if (root == HKEY_LOCAL_MACHINE && sub && !lstrcmpiA(sub, kGameKey) && out) { *out = FAKE_KEY; return ERROR_SUCCESS; }
    return oRegOpenKeyExA(root, sub, opt, sam, out);
}

static LONG RetValue(DWORD type, const void* data, DWORD size, LPDWORD ptype, LPBYTE out, LPDWORD psize)
{
    if (ptype) *ptype = type;
    if (!psize) return out ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!out) { *psize = size; return ERROR_SUCCESS; }
    if (*psize < size) { *psize = size; return ERROR_MORE_DATA; }
    memcpy(out, data, size); *psize = size;
    return ERROR_SUCCESS;
}

static LONG WINAPI hRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (key != FAKE_KEY) return oRegQueryValueExA(key, name, res, type, data, size);
    if (!name) return ERROR_FILE_NOT_FOUND;
    if (!lstrcmpiA(name, "version")) return RetValue(REG_SZ, kVersion, sizeof kVersion, type, data, size);
    if (!lstrcmpiA(name, "path"))    return RetValue(REG_SZ, g_dir, lstrlenA(g_dir) + 1, type, data, size);
    if (!lstrcmpiA(name, "set")) {
        DWORD v = (DWORD)GetPrivateProfileIntA("game", "set", 0, g_ini) & ~SET_FULLSCREEN;
        if (g_mode == MODE_FULLSCREEN) v |= SET_FULLSCREEN;
        g_gameFS = g_mode == MODE_FULLSCREEN;
        Log("game options read: set=%x", v);
        return RetValue(REG_DWORD, &v, 4, type, data, size);
    }
    return ERROR_FILE_NOT_FOUND;
}

static LONG WINAPI hRegSetValueExA(HKEY key, LPCSTR name, DWORD res, DWORD type, const BYTE* data, DWORD size)
{
    if (key != FAKE_KEY) return oRegSetValueExA(key, name, res, type, data, size);
    if (name && !lstrcmpiA(name, "set") && type == REG_DWORD && data && size >= 4) {
        DWORD v = *(const DWORD*)data & ~SET_FULLSCREEN;
        char buf[32]; wsprintfA(buf, "%u", v);
        WritePrivateProfileStringA("game", "set", buf, g_ini);
        Log("game options saved: set=%x", v);
    }
    return ERROR_SUCCESS;
}

static LONG WINAPI hRegCloseKey(HKEY key)
{
    if (key == FAKE_KEY) return ERROR_SUCCESS;
    return oRegCloseKey(key);
}

// The game minimizes itself whenever it loses focus (the old "disappears into the taskbar"
// problem) by sending WM_SYSCOMMAND/SC_MINIMIZE to its own window. Drop exactly those;
// minimizing from the title bar or the taskbar does not go through the game's imports.
static bool IsSelfMinimize(HWND h, UINT m, WPARAM w)
{
    if (m == WM_SYSCOMMAND && (w & 0xFFF0) == SC_MINIMIZE && h && h == g_top && !g_closing) {
        Log("ignored self-minimize");
        return true;
    }
    return false;
}

static BOOL WINAPI hPostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (IsSelfMinimize(h, m, w)) return TRUE;
    return oPostMessageA(h, m, w, l);
}

static LRESULT WINAPI hSendMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (IsSelfMinimize(h, m, w)) return 0;
    return oSendMessageA(h, m, w, l);
}

static void WINAPI hPostQuitMessage(int code)
{
    Log("PostQuitMessage(%d) thread=%u", code, GetCurrentThreadId());
    if (GetWindowThreadProcessId(g_top, NULL) == GetCurrentThreadId()) g_closing = true;
    oPostQuitMessage(code);
}

// =======================================================================================
// window hooks
// =======================================================================================
static const char* ClassOf(HWND h, char* buf, int n) { buf[0] = 0; if (h) GetClassNameA(h, buf, n); return buf; }

static HWND WINAPI hCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND r = oCreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    char c[64]; ClassOf(r, c, sizeof c);
    Log("CreateWindowEx '%s' ex=%08x style=%08x %d,%d %dx%d parent=%p -> %p", c, ex, style, x, y, w, h, parent, r);
    if (!r) return r;
    if (!lstrcmpA(c, "FlcAppClassNT") && !g_top) {
        g_top = r;
        g_topProc = (WNDPROC)SetWindowLongA(r, GWL_WNDPROC, (LONG)TopProc);
    } else if (!lstrcmpA(c, "Graphical System Child") && !g_view) {
        g_logW = w; g_logH = h;
        g_gamePos.x = x; g_gamePos.y = y;
        g_view = r;
        g_viewProc = (WNDPROC)SetWindowLongA(r, GWL_WNDPROC, (LONG)ViewProc);
        ApplyLayout();
    }
    return r;
}

static BOOL WINAPI hSetWindowPos(HWND hw, HWND after, int x, int y, int w, int h, UINT fl)
{
    if (hw && hw == g_top) {
        if (g_view) {
            // keep our own layout; only honour z-order / show requests
            if (fl & SWP_SHOWWINDOW) oShowWindow(hw, SW_SHOW);
            ApplyLayout();
            return TRUE;
        }
        return oSetWindowPos(hw, after, x, y, w, h, fl);
    }
    if (hw && hw == g_view) {
        if (!(fl & SWP_NOMOVE)) { g_gamePos.x = x; g_gamePos.y = y; }
        if (!(fl & SWP_NOSIZE) && w > 0 && h > 0 && (w != g_logW || h != g_logH)) Log("game resized picture to %dx%d (ignored)", w, h);
        if (fl & SWP_SHOWWINDOW) oShowWindow(hw, SW_SHOW);
        if (fl & SWP_HIDEWINDOW) oShowWindow(hw, SW_HIDE);
        return TRUE;
    }
    return oSetWindowPos(hw, after, x, y, w, h, fl);
}

static BOOL WINAPI hShowWindow(HWND hw, int cmd)
{
    if (hw && hw == g_top) Log("game ShowWindow(main, %d)", cmd);
    // The game minimizes itself whenever it is not the foreground window (that was the
    // old "game disappears into the taskbar" problem). The user can still minimize it.
    if (hw && hw == g_top && !g_closing &&
        (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE))
        return IsWindowVisible(hw);
    if (hw && hw == g_top && (cmd == SW_MAXIMIZE || cmd == SW_SHOWMAXIMIZED)) cmd = SW_SHOW;
    BOOL r = oShowWindow(hw, cmd);
    if (hw && hw == g_top && g_view && cmd != SW_HIDE && cmd != SW_MINIMIZE && cmd != SW_SHOWMINIMIZED) ApplyLayout();
    return r;
}

static BOOL WINAPI hGetClientRect(HWND hw, LPRECT r)
{
    if (hw && hw == g_view && r) { SetRect(r, 0, 0, g_logW, g_logH); return TRUE; }
    return oGetClientRect(hw, r);
}

static BOOL WINAPI hScreenToClient(HWND hw, LPPOINT p)
{
    if (hw && p && g_view && (hw == g_view || hw == g_top)) {
        POINT q = *p;
        BOOL r = oScreenToClient(g_view, &q);
        LONG x = q.x, y = q.y;
        ViewToGame(x, y);
        if (hw == g_top) { x += g_gamePos.x; y += g_gamePos.y; }
        p->x = x; p->y = y;
        return r;
    }
    return oScreenToClient(hw, p);
}


// =======================================================================================
// init
// =======================================================================================
static void MakeDpiAware()
{
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef BOOL (WINAPI* SPDAC)(HANDLE);
    SPDAC f = (SPDAC)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (f && (f((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */) || f((HANDLE)-3))) return;
    typedef BOOL (WINAPI* SPDA)();
    SPDA g = (SPDA)GetProcAddress(u, "SetProcessDPIAware");
    if (g) g();
}

static void HookModule(HMODULE m)
{
#define HOOK(dll, fn) PatchIAT(m, dll, #fn, (void*)h##fn, (void**)&o##fn)
    HOOK("GDI32.dll", GetDeviceCaps);
    HOOK("GDI32.dll", BitBlt);
    HOOK("GDI32.dll", StretchBlt);
    HOOK("GDI32.dll", PatBlt);
    HOOK("USER32.dll", EnumDisplaySettingsA);
    HOOK("USER32.dll", ChangeDisplaySettingsA);
    HOOK("USER32.dll", CreateWindowExA);
    HOOK("USER32.dll", SetWindowPos);
    HOOK("USER32.dll", ShowWindow);
    HOOK("USER32.dll", GetClientRect);
    HOOK("USER32.dll", ScreenToClient);
    HOOK("USER32.dll", GetMessageA);
    HOOK("USER32.dll", PostQuitMessage);
    HOOK("USER32.dll", PostMessageA);
    HOOK("USER32.dll", SendMessageA);
    HOOK("ADVAPI32.dll", RegOpenKeyExA);
    HOOK("ADVAPI32.dll", RegQueryValueExA);
    HOOK("ADVAPI32.dll", RegSetValueExA);
    HOOK("ADVAPI32.dll", RegCloseKey);
    HOOK("USER32.dll", PeekMessageA);
#undef HOOK
}

static void Init()
{
    HMODULE exe = GetModuleHandleA(NULL);
    GetModuleFileNameA(exe, g_dir, MAX_PATH);
    char* s = strrchr(g_dir, '\\'); if (s) s[1] = 0;
    lstrcpyA(g_ini, g_dir); lstrcatA(g_ini, "gagfix.ini");
    LoadConfig();
    if (cfg.log) {
        char p[MAX_PATH]; lstrcpyA(p, g_dir); lstrcatA(p, "gagfix.log");
        g_log = fopen(p, "w");
    }
    Log("gagfix loaded: mode=%d scale=%d smooth=%d aspect=%d", cfg.mode, cfg.scale, cfg.smooth, cfg.aspect);
    g_mode = cfg.mode;
    InitializeCriticalSection(&g_cs);

    MakeDpiAware();

    // make sure the originals of everything we call directly are resolved
    HMODULE u = GetModuleHandleA("user32.dll"), g = GetModuleHandleA("gdi32.dll"), k = GetModuleHandleA("kernel32.dll");
    oGetDeviceCaps = (GetDeviceCaps_t)GetProcAddress(g, "GetDeviceCaps");
    oBitBlt = (BitBlt_t)GetProcAddress(g, "BitBlt");
    oStretchBlt = (StretchBlt_t)GetProcAddress(g, "StretchBlt");
    oPatBlt = (PatBlt_t)GetProcAddress(g, "PatBlt");
    oEnumDisplaySettingsA = (EnumDisplaySettingsA_t)GetProcAddress(u, "EnumDisplaySettingsA");
    oChangeDisplaySettingsA = (ChangeDisplaySettingsA_t)GetProcAddress(u, "ChangeDisplaySettingsA");
    oCreateWindowExA = (CreateWindowExA_t)GetProcAddress(u, "CreateWindowExA");
    oSetWindowPos = (SetWindowPos_t)GetProcAddress(u, "SetWindowPos");
    oShowWindow = (ShowWindow_t)GetProcAddress(u, "ShowWindow");
    oGetClientRect = (GetClientRect_t)GetProcAddress(u, "GetClientRect");
    oScreenToClient = (ScreenToClient_t)GetProcAddress(u, "ScreenToClient");
    oGetSystemMetrics = (GetSystemMetrics_t)GetProcAddress(u, "GetSystemMetrics");
    oGetMessageA = (GetMessageA_t)GetProcAddress(u, "GetMessageA");
    oPostQuitMessage = (PostQuitMessage_t)GetProcAddress(u, "PostQuitMessage");
    oPostMessageA = (PostMessageA_t)GetProcAddress(u, "PostMessageA");
    oSendMessageA = (SendMessageA_t)GetProcAddress(u, "SendMessageA");
    HMODULE a = LoadLibraryA("advapi32.dll");
    oRegOpenKeyExA = (RegOpenKeyExA_t)GetProcAddress(a, "RegOpenKeyExA");
    oRegQueryValueExA = (RegQueryValueExA_t)GetProcAddress(a, "RegQueryValueExA");
    oRegSetValueExA = (RegSetValueExA_t)GetProcAddress(a, "RegSetValueExA");
    oRegCloseKey = (RegCloseKey_t)GetProcAddress(a, "RegCloseKey");
    oPeekMessageA = (PeekMessageA_t)GetProcAddress(u, "PeekMessageA");

    HookModule(exe);

    CloseHandle(CreateThread(NULL, 0, PresenterThread, NULL, 0, NULL));
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init();
    }
    return TRUE;
}
