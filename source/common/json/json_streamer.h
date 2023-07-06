#pragma once

#include <memory>
#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

class Streamer {
 public:
  Streamer(Buffer::Instance& response) : response_(response) {}

  // A Level represents the current map or array. We keep track
  // of what character is needed to close it, and whether or not
  // the first entry has been added.
  struct Level {
    Level(Streamer& streamer, absl::string_view opener, absl::string_view closer);
    ~Level();

   protected:
    void newEntryHelper();

    Streamer& streamer_;

   private:
    absl::string_view closer_;
    bool is_first_;
  };
  using LevelPtr = std::unique_ptr<Level>;

  struct Map : public Level {
    using Entries = absl::Span<const absl::string_view>;

    Map(Streamer& streamer) : Level(streamer, "{", "}") {}
    void newKey(absl::string_view name);
    void newEntries(Entries entries);
  };

  struct Array : public Level {
    Array(Streamer& streamer) : Level(streamer, "[", "]") {}
    void newEntry() { newEntryHelper(); }
  };

  Map& newMap();
  Array& newArray();
  void pop(Level& level);
  void clear();

  void addSanitized(absl::string_view token);
  void addCopy(absl::string_view token);
  void addNoCopy(absl::string_view token) { fragments_.push_back(token); }
  void addFragments(absl::Span<const absl::string_view> src);
  void flush();

 private:

  Buffer::Instance& response_;
  std::vector<absl::string_view> fragments_;
  std::string buffer_;
  std::stack<LevelPtr> levels_;
};

} // namespace Json
} // namespace Envoy
