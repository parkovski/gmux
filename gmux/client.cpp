#include "stdafx.h"
#include "gmux.h"

int ClientMain(std::wstring const &pipe_name) {
  HANDLE pipe;
  while (true) {
    pipe = CreateFileW(
      pipe_name.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr
    );
    if (pipe == INVALID_HANDLE_VALUE) {
      auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) {
        return -1;
      } else if (error == ERROR_PIPE_BUSY) {
        if (!WaitNamedPipeW(pipe_name.c_str(), 1000)) {
          std::wcerr << L"Pipe busy.\n";
          return 1;
        }
        continue;
      }
      std::wcerr << L"Client connection error.\n";
      return 1;
    }
    break;
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
    std::wcerr << L"Error setting pipe to message read mode.\n";
    CloseHandle(pipe);
    return 1;
  }
  DWORD bytesWritten;
  WriteFile(pipe, L"Hi server!", 20, &bytesWritten, nullptr);

  CloseHandle(pipe);
  return 0;
}
