#include "stdafx.h"
#include "commands.h"

// <>=====<>

class CmdQuit: public Command {
  DWORD thread_id;

public:
  CmdQuit(DWORD thread_id) : thread_id(thread_id) {}

  virtual void run(WindowMatch &) {
    PostThreadMessageW(this->thread_id, WM_QUIT, 0, 0);
  }
};

class CmdHi: public Command {
public:
  virtual void run(WindowMatch &) {
    std::wcout << L"Hi!" << std::endl;
  }
};

// <>=====<>

void Command::create_defaults(CommandMap &map, DWORD thread_id) {
  auto add = [&](Command *c, wchar_t *s, std::initializer_list<CommandKey> sh) {
    auto idx = map.add_command(c);
    map.add_name(idx, s);
    map.add_shortcut(idx, sh);
  };

  add(
    new CmdQuit(thread_id),
    L"quit",
    {CommandKey('A').set_ctrl(), CommandKey('D')}
  );

  add(
    new CmdHi,
    L"hi",
    {CommandKey('A').set_ctrl(), CommandKey('H')}
  );
}
