#include "stdafx.h"
#include "gmux.h"

void fix(std::wstring &s) {
  s.erase(std::find(s.begin(), s.end(), L'\0'), s.end());
}

BOOL FindShellWindows(HWND hWnd, LPARAM lParam);

struct WindowMatch {
  const std::vector<std::wstring> shells;
  std::vector<HWND> windows;

  WindowMatch(const std::initializer_list<std::wstring> shells) : shells(shells) {}

  void scan() {
    EnumDesktopWindows((HDESK)nullptr, FindShellWindows, (LPARAM)this);
  }
};

BOOL FindShellWindows(HWND hWnd, LPARAM lParam) {
  auto match = (WindowMatch *)lParam;
  static std::wstring title;
  static std::wstring processName;
  if (std::find(match->windows.begin(), match->windows.end(), hWnd) != match->windows.end()) {
    // We've already got this one.
    return true;
  }
  auto titleLen = GetWindowTextLengthW(hWnd);
  title.resize(titleLen + 1);
  GetWindowTextW(hWnd, &title[0], titleLen + 1);
  fix(title);
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

struct CommandKey {
  bool control;
  bool alt;
  bool shift;
  bool win;
  int key_code;

  CommandKey()
    : control(false), alt(false), shift(false), win(false), key_code(0)
  {}

  CommandKey(int vk)
    : control(false), alt(false), shift(false), win(false), key_code(vk)
  {}

  CommandKey(bool ctrl, bool alt, bool shift, bool win, int vk)
    : control(ctrl), alt(alt), shift(shift), win(win), key_code(vk)
  {}

  bool operator==(const CommandKey &other) {
    return this->control == other.control
      && this->alt == other.alt
      && this->shift == other.shift
      && this->win == other.win
      && this->key_code == other.key_code;
  }

  void clear() {
    this->control = false;
    this->alt = false;
    this->shift = false;
    this->win = false;
    this->key_code = 0;
  }
};

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
      this->state.control = true;
      return false;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.shift = true;
      return false;
    case VK_LMENU:
    case VK_RMENU:
      this->state.alt = true;
      return false;
    case VK_LWIN:
    case VK_RWIN:
      this->state.win = true;
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
    this->state.key_code = key;
    if (this->in_command == 1) {
      ++this->in_command;
    } else if (this->in_command > 1) {
      this->in_command = 0;
    } else if (this->state == this->leader) {
      this->in_command = 1;
    }
    return this->in_command;
  }

  bool key_up(int key) {
    if (!this->is_in_tracked_window()) {
      this->in_command = 0;
      this->state.clear();
      return false;
    }
    // check for commands...
    if (this->in_command == 2) {
      if (this->state == this->quit) {
        this->quitting = true;
      } else if (this->state == this->message) {
        std::wcout << L"Hello!\n";
      }
    }

    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
      this->state.control = false;
      return false;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.shift = false;
      return false;
    case VK_LMENU:
    case VK_RMENU:
      this->state.alt = false;
      return false;
    case VK_LWIN:
    case VK_RWIN:
      this->state.win = false;
      return false;
    default:
      this->state.key_code = 0;
      break;
    }

    return this->in_command;
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

DWORD WINAPI PipeListenerThread(void *param) {
  HANDLE pipe = (HANDLE)param;
  char buffer[PIPE_BUFFER_SIZE + 2];
  DWORD bytesRead;

  while (true) {
    if (!ConnectNamedPipe(pipe, nullptr)) {
      auto error = GetLastError();
      if (error != ERROR_PIPE_CONNECTED) {
        return error;
      }
    }
    auto result = ReadFile(pipe, buffer, PIPE_BUFFER_SIZE, &bytesRead, nullptr);
    if (!result || bytesRead == 0) {
      return GetLastError();
    }
    buffer[bytesRead] = buffer[bytesRead + 1] = 0;
    std::wcout << L"Message: " << reinterpret_cast<wchar_t *>(buffer) << L"\n";
    if (!DisconnectNamedPipe(pipe)) {
      return GetLastError();
    }
  }
}

// TODO: PostQuitMessage is per-thread, so need to send commands
// from the main thread.
int ServerMain(std::wstring const &pipe_name, HINSTANCE hInstance) {
  auto pipe = CreateNamedPipeW(
    pipe_name.c_str(),
    PIPE_ACCESS_DUPLEX,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
    2,
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
  cr.set_leader(CommandKey(true, false, false, false, 'A'));
  cr.scan_windows();
  command_recognizer = &cr;

  auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInstance, 0);
  if (hook == nullptr) {
    std::wcerr << L"Error creating keyboard hook.\n";
    CloseHandle(pipe);
    return 1;
  }

  auto pipeThread = CreateThread(
    nullptr,
    0,
    PipeListenerThread,
    (void *)pipe,
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
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(hook);
  CancelSynchronousIo(pipeThread);
  WaitForSingleObject(pipeThread, 100);
  DWORD exitCode;
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
