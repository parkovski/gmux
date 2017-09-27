#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <iterator>
#include <algorithm>
#include <Windows.h>

struct WindowMatch {
  const std::vector<std::wstring> shells;
  std::vector<HWND> windows;

  WindowMatch(const std::initializer_list<std::wstring> shells)
    : shells(shells)
  {}

  void scan();
};

class CommandKey {
  const int CONTROL = 0x00100000;
  const int ALT = 0x00200000;
  const int SHIFT = 0x00400000;
  const int WIN = 0x00800000;
  const int MASK = 0x00F00000;

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

  CommandKey &with_ctrl(bool with = true) {
    this->set_mask(CommandKey::CONTROL, with);
    return *this;
  }

  bool has_ctrl() const {
    return this->key & CommandKey::CONTROL;
  }

  CommandKey &with_alt(bool with = true) {
    this->set_mask(CommandKey::ALT, with);
    return *this;
  }

  bool has_alt() const {
    return this->key & CommandKey::ALT;
  }

  CommandKey &with_shift(bool with = true) {
    this->set_mask(CommandKey::SHIFT, with);
    return *this;
  }

  bool has_shift() const {
    return this->key & CommandKey::SHIFT;
  }

  CommandKey &with_win(bool with = true) {
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

class SequenceKey {
  unsigned position;
  CommandKey key;
  unsigned sequence;

public:
  SequenceKey(unsigned position, CommandKey key, unsigned sequence)
    : position(position), key(key), sequence(sequence)
  {}

  static bool less_key(SequenceKey const &self, SequenceKey const &other) {
    if (self.position < other.position) return true;
    return self.key < other.key;
  }

  bool operator==(SequenceKey const &other) const {
    return
      this->position == other.position
      && this->key == other.key
      && this->sequence == other.sequence;
  }

  bool operator!=(SequenceKey const &other) const {
    return !(*this == other);
  }

  /// Order is important here for lookup.
  bool operator<(SequenceKey const &other) const {
    if (this->position < other.position) return true;
    if (this->key < other.key) return true;
    if (this->sequence < other.sequence) return true;
    return false;
  }

  bool operator<=(SequenceKey const &other) const {
    return !(other < *this);
  }

  bool operator>(SequenceKey const &other) const {
    return other < *this;
  }

  bool operator>=(SequenceKey const &other) const {
    return !(*this < other);
  }
};

class CommandKeyMap {
  unsigned sequence;
  std::vector<SequenceKey> list;

public:
  CommandKeyMap()
    : sequence(0), list()
  {}

  template<typename Iter>
  void add(const Iter &iter) {
    unsigned position = 0;
    unsigned sequence = this->sequence;
    ++this->sequence;
    for (auto const &key: iter) {
      this->list.push_back(SequenceKey(position, sequence, key));
      ++position;
    }
  }

  unsigned sequence_count() const {
    return this->sequence;
  }

private:
  friend class KeySeqMatcher;
  void finish() {
    std::sort(this->list.begin(), this->list.end());
  }

  auto find(unsigned position, CommandKey key)
    -> std::pair<
      std::vector<SequenceKey>::const_iterator,
      std::vector<SequenceKey>::const_iterator
    >
  {
    return std::equal_range(
      this->list.cbegin(),
      this->list.cend(),
      SequenceKey(position, key, 0),
      &SequenceKey::less_key
    );
  }
};

class KeySeqMatcher {
  CommandKeyMap key_map;
  std::vector<bool> valid_sequences;
  unsigned position;

public:
  KeySeqMatcher(CommandKeyMap &&key_map)
    : key_map(key_map)
  {
    this->valid_sequences.resize(this->key_map.sequence_count());
    this->key_map.finish();
  }

  void begin() {
    auto size = this->valid_sequences.size();
    this->valid_sequences.assign(size, true);
    this->position = 0;
  }

  bool next(CommandKey key) {
    auto range = this->key_map.find(this->position, key);
    //...
  }
};
