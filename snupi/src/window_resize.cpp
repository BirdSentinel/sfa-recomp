#include "window_resize.h"
#include <windows.h>
#include <commctrl.h>
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window_sdl.h>
#include <stdexcept>

REXCVAR_DEFINE_BOOL(window_resize_selftest, false, "Debug",
                    "Exercise rendering inside a native modal resize loop");

namespace snupi::resize {
namespace {
constexpr UINT_PTR kSubclass = 0x534E5253;
constexpr UINT_PTR kPaintTimer = 0x534E5254, kTestTimer = 0x534E5255;
rex::ui::WindowSDL* window = nullptr;
bool resizing = false, painting = false;
unsigned paints = 0;

void Paint(HWND hwnd) {
  // Never enter the presenter recursively (DXGI can dispatch native messages).
  if (!resizing || painting || IsIconic(hwnd)) return;
  painting = true;
  struct Reset { ~Reset() { painting = false; } } reset;
  RECT client{};
  if (!GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0) return;
  // SDL's queued size notifications cannot reach the application loop while
  // DefWindowProc owns the modal move/resize loop. Update the surface first.
  SDL_Event event{};
  event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  event.window.data1 = client.right;
  event.window.data2 = client.bottom;
  window->HandleWindowEvent(event);
  event.type = SDL_EVENT_WINDOW_EXPOSED;
  window->HandleWindowEvent(event);
  ++paints;
}

void SelfTest(HWND hwnd) {
  RECT original{};
  GetWindowRect(hwnd, &original);
  const unsigned before = paints;
  SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
  // Deliberately pump only native messages, exactly like the Windows modal
  // loop: the normal SDL application loop cannot service paint requests here.
  const auto start = GetTickCount64();
  unsigned sizes = 0;
  while (GetTickCount64() - start < 1600) {
    if ((GetTickCount64() - start) / 160 > sizes) {
      ++sizes;
      SetWindowPos(hwnd, nullptr, 0, 0, 900 + int(sizes % 3) * 90,
                   600 + int(sizes % 2) * 70, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    MSG message{};
    if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(1);
  }
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
  SetWindowPos(hwnd, nullptr, original.left, original.top,
               original.right - original.left, original.bottom - original.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  if (paints - before < 5 || sizes < 5)
    throw std::runtime_error("Window resize self-test: rendering stalled in modal loop");
  REXLOG_INFO("Window resize self-test passed: {} paints across {} size changes in native modal loop",
              paints - before, sizes);
}

LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                            UINT_PTR, DWORD_PTR) {
  switch (message) {
    case WM_ENTERSIZEMOVE:
      resizing = true;
      SetTimer(hwnd, kPaintTimer, 16, nullptr);
      break;
    case WM_EXITSIZEMOVE:
      Paint(hwnd);
      resizing = false;
      KillTimer(hwnd, kPaintTimer);
      window->RequestPaint();
      break;
    case WM_TIMER:
      if (wparam == kPaintTimer) { Paint(hwnd); return 0; }
      if (wparam == kTestTimer) {
        KillTimer(hwnd, kTestTimer);
        SelfTest(hwnd);
        return 0;
      }
      break;
    case WM_NCDESTROY:
      resizing = false;
      KillTimer(hwnd, kPaintTimer);
      KillTimer(hwnd, kTestTimer);
      RemoveWindowSubclass(hwnd, Procedure, kSubclass);
      window = nullptr;
      break;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}
}

void Initialize(rex::ui::Window& app_window) {
  window = static_cast<rex::ui::WindowSDL*>(&app_window);
  const auto hwnd = static_cast<HWND>(window->GetNativeWindowHandle());
  if (!SetWindowSubclass(hwnd, Procedure, kSubclass, 0))
    throw std::runtime_error("Cannot install window resize handler");
  if (REXCVAR_GET(window_resize_selftest)) SetTimer(hwnd, kTestTimer, 8000, nullptr);
}
}
