#include "stdafx.h"
#include "commands.h"

BOOL FindShellWindows(HWND hWnd, LPARAM lParam);
void WindowMatch::scan() {
  EnumDesktopWindows((HDESK)nullptr, FindShellWindows, (LPARAM)this);
}

bool WindowMatch::is_in_tracked_window() const {
  auto foreground = GetForegroundWindow();
  for (auto window: this->windows) {
    if (foreground == window) return true;
  }
  return false;
}

// <>=====<>

bool KeyRecognizer::key_down(int key) {
  switch (key) {
  case VK_LCONTROL:
  case VK_RCONTROL:
    this->key.set_ctrl();
    return false;
  case VK_LSHIFT:
  case VK_RSHIFT:
    this->key.set_shift();
    return false;
  case VK_LMENU:
  case VK_RMENU:
    this->key.set_alt();
    return false;
  case VK_LWIN:
  case VK_RWIN:
    this->key.set_win();
    return false;
  default:
    this->ate_prev_key = false;
    this->key.set_key_code(key);
    return true;
  }
}

bool KeyRecognizer::key_up(int vk) {
  switch (vk) {
  case VK_LCONTROL:
  case VK_RCONTROL:
    this->key.set_ctrl(false);
    return false;
  case VK_LSHIFT:
  case VK_RSHIFT:
    this->key.set_shift(false);
    return false;
  case VK_LMENU:
  case VK_RMENU:
    this->key.set_alt(false);
    return false;
  case VK_LWIN:
  case VK_RWIN:
    this->key.set_win(false);
    return false;
  default:
    this->key.set_key_code(0);
    return this->ate_prev_key;
  }
}

void KeyRecognizer::ate_key() {
  this->ate_prev_key = true;
}

CommandKey KeyRecognizer::get_key() const {
  return this->key;
}

// <>=====<>

size_t CommandMap::add_command(Command *command) {
  this->commands.push_back(std::unique_ptr<Command>(command));
  return this->commands.size() - 1;
}

bool CommandMap::add_name(size_t command, std::wstring const &name) {
  if (this->names.find(name) != this->names.cend()) {
    return false;
  }
  this->names[name] = command;
  return true;
}

bool CommandMap::add_shortcut_parts(
  std::basic_string<CommandKey> const &shortcut
)
{
  std::basic_string<CommandKey> s;
  auto len = shortcut.size();
  s.reserve(len);
  for (size_t i = 0; i < len - 1; ++i) {
    s.push_back(shortcut[i]);
    auto iter = this->shortcuts.find(s);
    if (iter == this->shortcuts.cend()) {
      this->shortcuts[s] = CommandMap::PARTIAL;
    } else if (iter->second != CommandMap::PARTIAL) {
      return false;
    }
  }
  return true;
}

bool CommandMap::add_shortcut(
  size_t command,
  std::basic_string<CommandKey> const &shortcut
)
{
  if (this->shortcuts.find(shortcut) != this->shortcuts.cend()) {
    return false;
  }
  if (!this->add_shortcut_parts(shortcut)) {
    return false;
  }
  this->shortcuts[shortcut] = command;
  return true;
}

size_t CommandMap::get_command(std::wstring const &str) {
  auto const iter = this->names.find(str);
  if (iter == this->names.cend()) {
    return CommandMap::INVALID;
  }
  return iter->second;
}

size_t CommandMap::get_command(
  std::basic_string<CommandKey> const &shortcut
)
{
  auto const iter = this->shortcuts.find(shortcut);
  if (iter == this->shortcuts.cend()) {
    return CommandMap::INVALID;
  }
  return iter->second;
}

void CommandMap::run(size_t command, WindowMatch &wm) {
  if (command >= this->commands.size()) {
    return;
  }
  this->commands[command]->run(wm);
}
