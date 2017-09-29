#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <iterator>
#include <algorithm>
#include <memory>
#include <Windows.h>

struct WindowMatch {
  const std::vector<std::wstring> shells;
  std::vector<HWND> windows;

  WindowMatch(const std::initializer_list<std::wstring> shells)
    : shells(shells)
  {}

  void scan();
  bool is_in_tracked_window() const;
};

class CommandKey {
  static const int CONTROL = 0x00100000;
  static const int ALT = 0x00200000;
  static const int SHIFT = 0x00400000;
  static const int WIN = 0x00800000;
  static const int MASK = 0x00F00000;

  int key;

  void set_mask(int mask, bool set) {
    if (set) {
      this->key |= mask;
    } else {
      this-> key &= ~mask;
    }
  }

public:
  CommandKey()
    : key(0)
  {}

  CommandKey(int vk)
    : key(vk)
  {}

  CommandKey(CommandKey const &other)
    : key(other.key)
  {}

  CommandKey &set_ctrl(bool with = true) {
    this->set_mask(CommandKey::CONTROL, with);
    return *this;
  }

  bool has_ctrl() const {
    return this->key & CommandKey::CONTROL;
  }

  CommandKey &set_alt(bool with = true) {
    this->set_mask(CommandKey::ALT, with);
    return *this;
  }

  bool has_alt() const {
    return this->key & CommandKey::ALT;
  }

  CommandKey &set_shift(bool with = true) {
    this->set_mask(CommandKey::SHIFT, with);
    return *this;
  }

  bool has_shift() const {
    return this->key & CommandKey::SHIFT;
  }

  CommandKey &set_win(bool with = true) {
    this->set_mask(CommandKey::WIN, with);
    return *this;
  }

  bool has_win() const {
    return this->key & CommandKey::WIN;
  }

  int get_key_code() const {
    return this->key & ~CommandKey::MASK;
  }

  void set_key_code(int key) {
    auto mask = this->key & CommandKey::MASK;
    this->key = key | mask;
  }

  void clear() {
    this->key = 0;
  }

  CommandKey &operator=(const CommandKey &other) {
    this->key = other.key;
    return *this;
  }

  bool operator==(const CommandKey &other) const {
    return this->key == other.key;
  }

  bool operator!=(const CommandKey &other) const {
    return !(*this == other);
  }

  bool operator<(const CommandKey &other) const {
    return this->key < other.key;
  }

  bool operator<=(const CommandKey &other) const {
    return !(other < *this);
  }

  bool operator>(const CommandKey &other) const {
    return other < *this;
  }

  bool operator>=(const CommandKey &other) const {
    return !(*this < other);
  }
};

class KeyRecognizer {
  CommandKey key;
  bool ate_prev_key;

public:
  bool key_down(int vk);
  bool key_up(int vk);
  void ate_key();
  CommandKey get_key() const;
};


class Command;
class CommandMap {
  std::unordered_map<std::wstring, size_t> names;
  std::unordered_map<std::basic_string<CommandKey>, size_t> shortcuts;
  std::vector<std::unique_ptr<Command>> commands;

  bool add_shortcut_parts(
    std::basic_string<CommandKey> const &shortcut
  );
public:
  size_t add_command(Command *command);
  bool add_name(size_t command, std::wstring const &name);
  bool add_shortcut(
    size_t command,
    std::basic_string<CommandKey> const &shortcut
  );

  static const size_t PARTIAL = SIZE_MAX;
  static const size_t INVALID = SIZE_MAX - 1;

  size_t get_command(std::wstring const &str);
  size_t get_command(std::basic_string<CommandKey> const &shortcut);

  void run(size_t command, WindowMatch &wm);
};

class Command {
public:
  virtual void run(WindowMatch &wm) = 0;

  static void create_defaults(CommandMap &map, DWORD thread_id);
};
