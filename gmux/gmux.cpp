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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmd, int) {
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
  int argc;
  auto argv = CommandLineToArgvW(cmd, &argc);
  std::wstring args;
  for (int i = 0; i < argc; ++i) {
    args.append(argv[i]);
    if (i < argc - 1) {
      args.append(L" ");
    }
  }
  int ret = ClientMain(pipe_name, args);
  if (ret == -1) {
    ret = ServerMain(pipe_name, hInstance);
  }
  if (ret != 0) {
    std::wcerr << L"Couldn't start.\n";
  }
  return ret;
}
