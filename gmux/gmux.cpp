// server.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "gmux.h"

LRESULT CALLBACK KeyboardHook(int, WPARAM, LPARAM);

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

/*
class CommandMap {
  int leader;
  // (key, position) -> [command]
  // key == 0 for end.
  std::unordered_map<std::tuple<int, int>, std::set<int>> key_index;
  mutable bool in_command;
  mutable std::set<int> lookup_commands;
  mutable int lookup_position;

public:
  static const int CONTROL = 0x00100000;
  static const int SHIFT = 0x00200000;
  static const int ALT = 0x00400000;
  static const int WIN = 0x00800000;
  static const int MASK = 0x00F00000;

  /// Format: %C = Control, %S = Shift, %A = Alt, %W = Win, %% = %.
  void add(const char *command, size_t length, int command_index) {
    int position = 0;
    int key_position = 0;
    while (position < length) {
      int key = 0;
      while (position < length + 1 && command[position] == '%') {
        ++position;
        switch (command[position]) {
        case 'C':
          key |= CONTROL;
          break;
        case 'S':
          key |= SHIFT;
          break;
        case 'A':
          key |= ALT;
          break;
        case 'W':
          key |= WIN;
          break;
        case '%':
          key = (key & MASK) | '%';
          ++position;
          goto no_more_modifiers;
        default:
          key = (key & MASK) | '%';
          goto no_more_modifiers;
        }
        ++position;
      }
      key = (key & MASK) | command[position];
      ++position;
      no_more_modifiers:
      auto index = std::make_tuple(key, key_position);
      this->key_index[index].insert(command_index);
      ++key_position;
    }
    this->key_index[std::make_tuple(0, key_position)].insert(command_index);
  }

  /// >= 0 = Command index
  /// -1 = Ok, continue
  /// -2 = Not found
  int has_next_key(int ch, bool ctrl, bool shift, bool alt, bool win) const {
    if (ctrl) ch |= CommandMap::CONTROL;
    if (shift) ch |= CommandMap::SHIFT;
    if (alt) ch |= CommandMap::ALT;
    if (win) ch |= CommandMap::WIN;

    if (!this->in_command) {
      if (ch == this->leader) {
        this->in_command = true;
        return -1;
      } else {
        return -2;
      }
    }

    auto const key = std::make_tuple(this->lookup_position, ch);
    auto const commands = this->key_index.find(key);
    if (commands == this->key_index.end()) {
      this->in_command = false;
      this->lookup_commands.clear();
      this->lookup_position = 0;
      return -2;
    }

    auto const commands_for_key = commands->second;
    if (this->lookup_position == 0) {
      this->lookup_commands.insert(commands_for_key.begin(), commands_for_key.end());
    } else {
      auto item = this->lookup_commands.begin();
      while (item != this->lookup_commands.end()) {
        if (commands_for_key.find(*item) == commands_for_key.end()) {
          item = this->lookup_commands.erase(item);
        } else {
          ++item;
        }
      }
    }
  }

  int get(const char *command, size_t length) const {
    int position = 0;
    while (position < length) {
      // TODO: Find
      ++position;
    }
    return -1;
  }

  int get(std::string const &command) const {
    return this->get(command.c_str(), command.size());
  }
};
*/

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

  bool overlaps(const CommandKey &other) {
    return (!this->control | other.control)
      | (!this->alt | other.alt)
      | (!this->shift | other.shift)
      | (!this->win | other.win)
      | !(this->key_code ^ other.key_code);
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
    bool soft_key = true;
    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
      this->state.control = true;
      break;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.shift = true;
      break;
    case VK_LWIN:
    case VK_RWIN:
      this->state.win = true;
      break;
    default:
      soft_key = false;
      break;
    }

    if (!this->is_in_tracked_window()) {
      this->in_command = 0;
      this->expecting_leader_up = false;
      this->state.key_code = 0;
      return false;
    }
    if (!soft_key || this->state.key_code == 0) {
      this->state.key_code = key;
    }
    if (this->in_command == 1) {
      if (!soft_key) {
        ++this->in_command;
      }
    } else if (this->in_command > 1) {
      if (!soft_key) {
        this->in_command = 0;
      }
    } else if (this->state == this->leader) {
      this->in_command = 1;
    }
    return this->in_command;
  }

  // TODO: Rewrite these to eat input from the leader until the end of a command
  // or a cancel.
  bool key_up(int key) {
    if (!this->is_in_tracked_window()) {
      this->in_command = 0;
      this->state.key_code = 0;
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
      break;
    case VK_LSHIFT:
    case VK_RSHIFT:
      this->state.shift = false;
      break;
    case VK_LWIN:
    case VK_RWIN:
      this->state.win = false;
      break;
    default:
      std::wcout << L"Key up: " << (wchar_t)key << L"\n";
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
    return 1;
  }

  BindHandles();

  MSG msg;
  CommandRecognizer cr{L"powershell.exe", L"cmd.exe"};
  cr.set_leader(CommandKey(true, false, false, false, 'A'));
  cr.scan_windows();
  std::wcout << L"Found " << cr.get_match().windows.size() << L" windows\n";

  command_recognizer = &cr;
  auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInstance, 0);

  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(hook);

  return (int) msg.wParam;
}
