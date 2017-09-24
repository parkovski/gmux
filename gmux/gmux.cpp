#include "stdafx.h"
#include "gmux.h"

LRESULT CALLBACK KeyboardHook(int, WPARAM, LPARAM);


void BindHandles() {
  freopen("CONIN$", "r", stdin);
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);

  std::cout.clear();
  std::wcout.clear();
  std::cerr.clear();
  std::wcerr.clear();
  std::cin.clear();
  std::wcin.clear();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    MessageBoxW(
      (HWND)nullptr,
      L"Couldn't bind to console window",
      L"Error",
      MB_ICONERROR | MB_OK
    );
    return 1;
  }

  BindHandles();

  std::wstring pipe_name = L"\\\\.\\pipe\\gmux-the-named-pipe";
  int ret = ClientMain(pipe_name);
  if (ret == -1) {
    ret = ServerMain(pipe_name, hInstance);
  }
  return ret;
}
