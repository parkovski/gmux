#include "stdafx.h"
#include "commands.h"
#include "gmux.h"

void fix(std::wstring &s) {
  s.erase(std::find(s.begin(), s.end(), L'\0'), s.end());
}

BOOL FindShellWindows(HWND hWnd, LPARAM lParam);

void WindowMatch::scan() {
  EnumDesktopWindows((HDESK)nullptr, FindShellWindows, (LPARAM)this);
}

BOOL FindShellWindows(HWND hWnd, LPARAM lParam) {
  auto match = (WindowMatch *)lParam;
  static std::wstring processName;
  if (std::find(match->windows.begin(), match->windows.end(), hWnd) != match->windows.end()) {
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
  fix(processName);
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

class CommandRecognizer {
  WindowMatch match;
  CommandKey state;
  CommandKey leader;
  int in_command;
  bool expecting_leader_up;
  bool quitting;

  const CommandKey quit;
  const CommandKey message;

public:
  CommandRecognizer(std::initializer_list<std::wstring> shells)
    : match(shells),
      in_command(0),
      expecting_leader_up(false),
      quitting(false),
      quit(CommandKey('D')),
      message(CommandKey('M'))
  {}

  void set_leader(CommandKey const &leader) {
    this->leader = leader;
  }

  void scan_windows() {
    this->match.scan();
  }

  WindowMatch &get_match() {
    return this->match;
  }

  bool is_in_tracked_window() const {
    auto foreground = GetForegroundWindow();
    for (auto window: this->match.windows) {
      if (foreground == window) return true;
    }
    return false;
  }

  bool key_down(int key) {
    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
      this->state.with_ctrl();
      return false;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.with_shift();
      return false;
    case VK_LMENU:
    case VK_RMENU:
      this->state.with_alt();
      return false;
    case VK_LWIN:
    case VK_RWIN:
      this->state.with_win();
      return false;
    default:
      break;
    }

    if (!this->is_in_tracked_window()) {
      this->in_command = 0;
      this->expecting_leader_up = false;
      this->state.clear();
      return false;
    }
    this->state.set_key_code(key);
    if (this->in_command == 1) {
      ++this->in_command;
      return true;
    } else if (this->in_command > 1) {
      this->in_command = 0;
    }
    if (this->state == this->leader) {
      this->in_command = 1;
      return true;
    }
    return false;
  }

  bool key_up(int key) {
    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
      this->state.with_ctrl(false);
      return false;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.with_shift(false);
      return false;
    case VK_LMENU:
    case VK_RMENU:
      this->state.with_alt(false);
      return false;
    case VK_LWIN:
    case VK_RWIN:
      this->state.with_win(false);
      return false;
    default:
      break;
    }

    bool ate_prev_key = this->state.get_key_code() == key;
    // check for commands...
    if (this->in_command == 2) {
      if (this->state == this->quit) {
        this->quitting = true;
      } else if (this->state == this->message) {
        std::wcout << L"Hello!\n";
      }
    }

    this->state.set_key_code(0);
    return ate_prev_key;
  }

  bool should_quit() const {
    return this->quitting;
  }
};

static CommandRecognizer *command_recognizer;

LRESULT CALLBACK KeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0) {
    return CallNextHookEx((HHOOK)nullptr, nCode, wParam, lParam);
  }
  auto hook = (KBDLLHOOKSTRUCT *)lParam;
  auto code = hook->vkCode;
  bool eat = false;
  if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
    eat = command_recognizer->key_down(code);
  } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    eat = command_recognizer->key_up(code);
  }
  if (command_recognizer->should_quit()) {
    PostQuitMessage(0);
  }
  if (eat) {
    return 1;
  } else {
    return CallNextHookEx((HHOOK)nullptr, nCode, wParam, lParam);
  }
}

struct SharedMessageState {
  const HANDLE pipe;
  const HANDLE mutex;
  const HANDLE event;
  std::wstring message;

  SharedMessageState(HANDLE pipe)
    : pipe(pipe),
      mutex(CreateMutexW(nullptr, false, nullptr)),
      event(CreateEventW(nullptr, true, false, nullptr))
  {}

  ~SharedMessageState() {
    CloseHandle(this->mutex);
    CloseHandle(this->event);
  }
};

DWORD WINAPI PipeListenerThread(void *param) {
  auto state = (SharedMessageState *)param;
  char buffer[PIPE_BUFFER_SIZE];
  std::wstring message;
  DWORD bytesRead;

  while (true) {
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
          message.append(reinterpret_cast<wchar_t *>(buffer), bytesRead / sizeof(wchar_t));
          continue;
        }
        DisconnectNamedPipe(state->pipe);
        return error;
      } else {
        break;
      }
    }

    DWORD waitResult;
    while (true) {
      waitResult = WaitForSingleObject(state->mutex, 1000);
      switch (waitResult) {
      case WAIT_OBJECT_0:
        break;
      case WAIT_TIMEOUT:
        DisconnectNamedPipe(state->pipe);
        return ERROR_TIMEOUT;
      case WAIT_FAILED:
        DisconnectNamedPipe(state->pipe);
        return GetLastError();
      default:
        DisconnectNamedPipe(state->pipe);
        return ERROR_CANT_WAIT;
      }

      if (!state->message.empty()) {
        ReleaseMutex(state->mutex);
        Sleep(50);
        continue;
      }

      state->message = message;
      ReleaseMutex(state->mutex);
      SetEvent(state->event);
      break;
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
    std::wcerr << L"Error creating named pipe.\n";
    return 1;
  }

  CommandRecognizer cr{L"powershell.exe", L"cmd.exe"};
  cr.set_leader(CommandKey('A').with_ctrl());
  cr.scan_windows();
  command_recognizer = &cr;

  auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInstance, 0);
  if (hook == nullptr) {
    std::wcerr << L"Error creating keyboard hook.\n";
    CloseHandle(pipe);
    return 1;
  }

  SharedMessageState messageState(pipe);

  auto pipeThread = CreateThread(
    nullptr,
    0,
    PipeListenerThread,
    (void *)&messageState,
    0,
    nullptr
  );

  if (pipeThread == nullptr) {
    std::wcerr << L"Error creating server listener thread.\n";
    CloseHandle(pipe);
    UnhookWindowsHookEx(hook);
    return 1;
  }


  MSG msg;
  DWORD exitCode;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (WaitForSingleObject(messageState.event, 0) == WAIT_OBJECT_0) {
      switch (WaitForSingleObject(messageState.mutex, 0)) {
      case WAIT_OBJECT_0:
        std::wcout << L"Message: " << messageState.message << L"\n";
        messageState.message.clear();
        ResetEvent(messageState.event);
        ReleaseMutex(messageState.mutex);
        break;
      case WAIT_TIMEOUT:
        break;
      default:
        std::wcerr << L"Wait mutex failed\n";
        PostQuitMessage(1);
        break;
      }
    }
    if (GetExitCodeThread(pipeThread, &exitCode) && exitCode != STILL_ACTIVE) {
      std::wcerr << L"Listener thread error.\n";
      PostQuitMessage(1);
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(hook);
  CancelSynchronousIo(pipeThread);
  WaitForSingleObject(pipeThread, 100);
  GetExitCodeThread(pipeThread, &exitCode);
  if (exitCode == STILL_ACTIVE) {
    std::wcerr << L"The thread didn't exit.\n";
    TerminateThread(pipeThread, 0);
  }
  CloseHandle(pipeThread);
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);

  return (int) msg.wParam;
}
