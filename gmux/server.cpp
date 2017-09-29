#include "stdafx.h"
#include "commands.h"
#include "gmux.h"

class SharedMessageState {
public:
  const DWORD main_thread_id;
  const HANDLE pipe;
  const HANDLE event_cmd_ready;
  std::atomic_size_t cmd_index;
  std::basic_string<CommandKey> cmd_shortcut;
  WindowMatch win_match;
  CommandMap cmd_map;
  KeyRecognizer key_rec;

  SharedMessageState(DWORD main_thread_id, HANDLE pipe, WindowMatch &&win_match)
    : main_thread_id(main_thread_id),
      pipe(pipe),
      event_cmd_ready(CreateEventW(nullptr, true, false, nullptr)),
      win_match(std::move(win_match))
  {
    Command::create_defaults(this->cmd_map, this->main_thread_id);
    this->win_match.scan();
  }

  ~SharedMessageState() {
    DisconnectNamedPipe(this->pipe);
    CloseHandle(this->pipe);
    CloseHandle(this->event_cmd_ready);
  }
};

static SharedMessageState *shared_state;

BOOL FindShellWindows(HWND hWnd, LPARAM lParam) {
  auto match = (WindowMatch *)lParam;
  static std::wstring processName;
  if (
    std::find(match->windows.cbegin(), match->windows.cend(), hWnd)
      != match->windows.cend()
  )
  {
    // We've already got this one.
    return true;
  }
  DWORD processId;
  GetWindowThreadProcessId(hWnd, &processId);
  auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
  DWORD processLen = MAX_PATH + 1;
  processName.clear();
  processName.resize(processLen);
  QueryFullProcessImageNameW(process, 0, &processName[0], &processLen);
  processName.erase(
    std::find(processName.begin(), processName.end(), L'\0'),
    processName.end()
  );
  CloseHandle(process);

  for (auto const &shell: match->shells) {
    if (processName.size() < shell.size()) {
      continue;
    }
    auto sub = processName.substr(processName.size() - shell.size(), std::wstring::npos);
    if (sub == shell) {
      match->windows.push_back(hWnd);
    }
  }

  return true;
}

LRESULT CALLBACK KeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0) {
    return CallNextHookEx((HHOOK)nullptr, nCode, wParam, lParam);
  }
  auto hook = (KBDLLHOOKSTRUCT *)lParam;
  auto code = hook->vkCode;
  bool eat = false;
  if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
    eat = shared_state->key_rec.key_down(code);
    if (!shared_state->win_match.is_in_tracked_window()) {
      eat = false;
    } else if (eat) {
      shared_state->cmd_shortcut.push_back(shared_state->key_rec.get_key());
      auto result = shared_state->cmd_map.get_command(shared_state->cmd_shortcut);
      if (result == CommandMap::INVALID) {
        eat = false;
        shared_state->cmd_shortcut.clear();
      } else if (result == CommandMap::PARTIAL) {
        shared_state->key_rec.ate_key();
      } else {
        shared_state->cmd_shortcut.clear();
        shared_state->key_rec.ate_key();
        shared_state->cmd_index.store(result);
        SetEvent(shared_state->event_cmd_ready);
      }
    }
  } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    eat = shared_state->key_rec.key_up(code);
  }
  if (eat) {
    return 1;
  } else {
    return CallNextHookEx((HHOOK)nullptr, nCode, wParam, lParam);
  }
}

DWORD WINAPI CommandDispatchThread(void *param) {
  auto state = (SharedMessageState *)param;
  while (true) {
    auto result = WaitForSingleObject(state->event_cmd_ready, INFINITE);
    if (result == WAIT_OBJECT_0) {
      size_t command = state->cmd_index.load();
      state->cmd_map.run(command, state->win_match);
    }
    ResetEvent(state->event_cmd_ready);
  }
}

DWORD WINAPI PipeListenerThread(void *param) {
  auto state = (SharedMessageState *)param;
  char buffer[PIPE_BUFFER_SIZE];
  std::wstring message;
  DWORD bytesRead;

  while (true) {
    reconnect:
    if (!ConnectNamedPipe(state->pipe, nullptr)) {
      auto error = GetLastError();
      if (error != ERROR_PIPE_CONNECTED) {
        return error;
      }
    }

    bytesRead = 0;
    message.clear();
    while (true) {
      auto result = ReadFile(state->pipe, buffer, PIPE_BUFFER_SIZE, &bytesRead, nullptr);
      if (!result || bytesRead == 0) {
        auto error = GetLastError();
        if (error == ERROR_MORE_DATA) {
          message.append(
            reinterpret_cast<wchar_t *>(buffer),
            bytesRead / sizeof(wchar_t)
          );
          continue;
        }
        DisconnectNamedPipe(state->pipe);
        goto reconnect;
      } else {
        message.append(
          reinterpret_cast<wchar_t *>(buffer),
          bytesRead / sizeof(wchar_t)
        );
        break;
      }
    }

    auto result = state->cmd_map.get_command(message);
    if (result != CommandMap::INVALID && result != CommandMap::PARTIAL) {
      state->cmd_index.store(result);
      SetEvent(state->event_cmd_ready);
    }

    if (!DisconnectNamedPipe(state->pipe)) {
      return GetLastError();
    }
  }
}

// TODO: Move everything except message loop to pipe thread.
int ServerMain(std::wstring const &pipe_name, HINSTANCE hInstance) {
  auto pipe = CreateNamedPipeW(
    pipe_name.c_str(),
    PIPE_ACCESS_DUPLEX,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
    3,
    PIPE_BUFFER_SIZE,
    PIPE_BUFFER_SIZE,
    0,
    nullptr
  );

  if (pipe == INVALID_HANDLE_VALUE) {
    std::wcerr << L"Error creating named pipe." << std::endl;
    return 1;
  }

  SharedMessageState messageState(
    GetCurrentThreadId(),
    pipe,
    WindowMatch{L"powershell.exe", L"cmd.exe"}
  );
  shared_state = &messageState;

  auto pipeThread = CreateThread(
    nullptr,
    0,
    PipeListenerThread,
    (void *)&messageState,
    0,
    nullptr
  );

  if (pipeThread == nullptr) {
    std::wcerr << L"Error creating server listener thread." << std::endl;
    CloseHandle(pipe);
    return 1;
  }

  auto commandThread = CreateThread(
    nullptr,
    0,
    CommandDispatchThread,
    (void *)&messageState,
    0,
    nullptr
  );

  if (commandThread == nullptr) {
    std::wcerr << L"Error creating server command thread." << std::endl;
    CancelSynchronousIo(pipeThread);
    TerminateThread(pipeThread, 0);
    return 1;
  }

  auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInstance, 0);
  if (hook == nullptr) {
    std::wcerr << L"Error creating keyboard hook." << std::endl;
    TerminateThread(commandThread, 0);
    CancelSynchronousIo(pipeThread);
    TerminateThread(pipeThread, 0);
    return 1;
  }

  MSG msg;
  DWORD exitCode;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(hook);
  CancelSynchronousIo(pipeThread);
  WaitForSingleObject(pipeThread, 100);
  GetExitCodeThread(pipeThread, &exitCode);
  if (exitCode == STILL_ACTIVE) {
    std::wcerr << L"The thread didn't exit." << std::endl;
    TerminateThread(pipeThread, 0);
  }
  CloseHandle(pipeThread);
  TerminateThread(commandThread, 0);
  CloseHandle(commandThread);

  return (int) msg.wParam;
}
