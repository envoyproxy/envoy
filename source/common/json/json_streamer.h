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
    void newEntry();

    Streamer& streamer_;
    absl::string_view closer_;
    bool is_first_;
  };
  using LevelPtr = std::unique_ptr<Level>;

  LevelPtr newMap() { return std::make_unique<Level>(*this, "{", "}"); }
  LevelPtr newArray() { return std::make_unique<Level>(*this, "[", "]"); }

  void addSanitized(absl::string_view token);
  void addLiteralCopy(absl::string_view token);
  void addLiteralNoCopy(absl::string_view token) { fragments_.push_back(token); }
  void addFragments(absl::Span<const absl::string_view> src) {
    fragments.insert(fragments_.end(), src.begin(), src.end());
  }
  void flush();

 private:

  Buffer::Instance& response_;
  std::vector<absl::string_view> fragments_;
  std::string buffer_;
};

} // namespace Json
} // namespace Envoy
