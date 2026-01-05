#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <windows.h>


#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define ID_TOGGLE_BTN 1001
#define ID_EXIT_BTN 1002

namespace Config {
float SMOOTHING = 0.45f;
int UPDATE_RATE_MS = 1;
} // namespace Config

struct Vec2 {
  float x, y;
  Vec2() : x(0), y(0) {}
  Vec2(float x, float y) : x(x), y(y) {}
  Vec2 operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
  Vec2 operator-(const Vec2 &o) const { return Vec2(x - o.x, y - o.y); }
  Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
  float magnitude() const { return sqrtf(x * x + y * y); }
};

class MouseSmoother {
private:
  std::atomic<bool> running;
  std::atomic<bool> enabled;
  std::thread workerThread;

  Vec2 targetPos;
  Vec2 smoothedPos;

  std::mutex stateMtx;
  HHOOK mouseHook;

public:
  static MouseSmoother *instance;

  MouseSmoother() : mouseHook(NULL) {
    running.store(false);
    enabled.store(false);
    instance = this;

    POINT pt;
    GetCursorPos(&pt);
    targetPos = Vec2((float)pt.x, (float)pt.y);
    smoothedPos = targetPos;
  }

  ~MouseSmoother() { stop(); }

  static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam,
                                            LPARAM lParam) {
    if (nCode >= 0 && instance && instance->enabled.load()) {
      MSLLHOOKSTRUCT *pMouse = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);

      if (pMouse->flags & LLMHF_INJECTED) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
      }

      if (wParam == WM_MOUSEMOVE) {
        instance->onMouseMove(pMouse->pt);
        return 1;
      }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }

  void onMouseMove(POINT pt) {
    POINT cur;
    GetCursorPos(&cur);

    float dx = (float)(pt.x - cur.x);
    float dy = (float)(pt.y - cur.y);

    if (dx == 0 && dy == 0)
      return;

    std::lock_guard<std::mutex> lock(stateMtx);
    targetPos.x += dx;
    targetPos.y += dy;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    targetPos.x = std::max(0.0f, std::min((float)screenW - 1, targetPos.x));
    targetPos.y = std::max(0.0f, std::min((float)screenH - 1, targetPos.y));
  }

  void workerLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (running.load()) {
      if (enabled.load()) {
        stateMtx.lock();

        Vec2 diff = targetPos - smoothedPos;
        float dist = diff.magnitude();

        float factor = Config::SMOOTHING;

        if (dist > 0.1f) {
          smoothedPos = smoothedPos + diff * factor;

          int scrX = (int)roundf(smoothedPos.x);
          int scrY = (int)roundf(smoothedPos.y);

          POINT cur;
          GetCursorPos(&cur);
          if (cur.x != scrX || cur.y != scrY) {
            SetCursorPos(scrX, scrY);
          }
        } else {
          if (dist > 0.001f) {
            smoothedPos = targetPos;
          }
        }
        stateMtx.unlock();
      } else {
        POINT pt;
        GetCursorPos(&pt);
        stateMtx.lock();
        smoothedPos = Vec2((float)pt.x, (float)pt.y);
        targetPos = smoothedPos;
        stateMtx.unlock();
      }

      std::this_thread::sleep_for(
          std::chrono::milliseconds(Config::UPDATE_RATE_MS));
    }
  }

  void start() {
    if (running.load())
      return;

    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc,
                                 GetModuleHandle(NULL), 0);
    if (!mouseHook) {
      MessageBoxA(NULL, "Failed to install hook!", "Error", MB_ICONERROR);
      return;
    }

    running.store(true);
    enabled.store(true);

    POINT pt;
    GetCursorPos(&pt);
    stateMtx.lock();
    targetPos = Vec2((float)pt.x, (float)pt.y);
    smoothedPos = targetPos;
    stateMtx.unlock();

    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    workerThread = std::thread(&MouseSmoother::workerLoop, this);
  }

  void stop() {
    running.store(false);
    if (workerThread.joinable())
      workerThread.join();

    if (mouseHook) {
      UnhookWindowsHookEx(mouseHook);
      mouseHook = NULL;
    }
    enabled.store(false);
  }

  void toggle() {
    if (!running.load()) {
      start();
    } else {
      bool s = !enabled.load();
      enabled.store(s);
      if (s) {
        POINT pt;
        GetCursorPos(&pt);
        stateMtx.lock();
        targetPos = Vec2((float)pt.x, (float)pt.y);
        smoothedPos = targetPos;
        stateMtx.unlock();
      }
    }
  }

  bool isEnabled() const { return enabled.load(); }
};

MouseSmoother *MouseSmoother::instance = nullptr;
MouseSmoother *g_App = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_SHIFT, 'T');
    RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT, 'Q');
    RegisterHotKey(hwnd, 3, MOD_CONTROL | MOD_SHIFT, '9');
    break;
  case WM_HOTKEY:
    if (wParam == 1 && g_App) {
      g_App->toggle();
      SetWindowTextA(GetDlgItem(hwnd, 10),
                     g_App->isEnabled() ? "ACTIVE" : "INACTIVE");
    }
    if (wParam == 2)
      PostQuitMessage(0);
    if (wParam == 3)
      ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
    break;
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_TOGGLE_BTN && g_App) {
      g_App->toggle();
      SetWindowTextA(GetDlgItem(hwnd, 10),
                     g_App->isEnabled() ? "ACTIVE" : "INACTIVE");
    }
    if (LOWORD(wParam) == ID_EXIT_BTN)
      PostQuitMessage(0);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  case WM_CTLCOLORSTATIC:
    SetTextColor((HDC)wParam, RGB(220, 220, 220));
    SetBkColor((HDC)wParam, RGB(30, 30, 40));
    return (LRESULT)GetStockObject(NULL_BRUSH);
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = WindowProc;
  wc.lpszClassName = "MouseSmootherClass";
  wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 40));
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassA(&wc);

  int scrW = GetSystemMetrics(SM_CXSCREEN);
  int scrH = GetSystemMetrics(SM_CYSCREEN);
  HWND hwnd =
      CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "3a9faah Mouse",
                      WS_POPUP | WS_VISIBLE | WS_BORDER, (scrW - 300) / 2,
                      (scrH - 200) / 2, 300, 200, 0, 0, hInst, 0);

  g_App = new MouseSmoother();
  g_App->start();

  CreateWindowA("STATIC", "3a9faah Mouse", WS_CHILD | WS_VISIBLE | SS_CENTER, 0,
                20, 300, 30, hwnd, 0, hInst, 0);
  CreateWindowA("STATIC", "ACTIVE", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 60,
                300, 30, hwnd, (HMENU)10, hInst, 0);

  CreateWindowA("BUTTON", "Toggle", WS_CHILD | WS_VISIBLE, 50, 100, 90, 40,
                hwnd, (HMENU)ID_TOGGLE_BTN, hInst, 0);
  CreateWindowA("BUTTON", "Exit", WS_CHILD | WS_VISIBLE, 160, 100, 90, 40, hwnd,
                (HMENU)ID_EXIT_BTN, hInst, 0);
  CreateWindowA("STATIC", "Ctrl+Shift+T : Toggle",
                WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 150, 300, 20, hwnd, 0,
                hInst, 0);

  MSG msg;
  while (GetMessage(&msg, 0, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  delete g_App;
  return (int)msg.wParam;
}
