#pragma once

#include <memory>
#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

class Streamer {
public:
  explicit Streamer(Buffer::Instance& response) : response_(response) {}

  struct Array;
  using ArrayPtr = std::unique_ptr<Array>;
  struct Map;
  using MapPtr = std::unique_ptr<Map>;

  // A Level represents the current map or array. We keep track
  // of what character is needed to close it, and whether or not
  // the first entry has been added.
  struct Level {
    Level(Streamer& streamer, absl::string_view opener, absl::string_view closer);
    virtual ~Level();

    virtual void newEntry() = 0;
    MapPtr newMap();
    ArrayPtr newArray();
    void close();

  protected:
    Streamer& streamer_;
    bool is_first_{true};
    bool is_closed_{false};

  private:
    absl::string_view closer_;
  };
  using LevelPtr = std::unique_ptr<Level>;

  struct Map : public Level {
    using NameValue = std::pair<const absl::string_view, const absl::string_view>;
    using Entries = absl::Span<const NameValue>;

    struct DeferredValue {
      explicit DeferredValue(Map& map);
      ~DeferredValue();
      void close();
      Map& map_;
      bool managed_{true};
    };
    using DeferredValuePtr = std::unique_ptr<DeferredValue>;

    Map(Streamer& streamer) : Level(streamer, "{", "}") {}
    ~Map() override;
    void newKey(absl::string_view name, std::function<void()>);
    DeferredValuePtr deferValue();
    void newEntries(const Entries& entries);
    virtual void newEntry() override;
    void newSanitizedValue(absl::string_view value);
    void clearDeferredValue() {
      deferred_value_.reset();
      expecting_value_ = false;
    }
    void addSanitized(absl::string_view value);

    bool expecting_value_{false};
    OptRef<DeferredValue> deferred_value_;
  };

  struct Array : public Level {
    Array(Streamer& streamer) : Level(streamer, "[", "]") {}
    using Strings = absl::Span<const absl::string_view>;
    void newEntries(const Strings& entries);
    virtual void newEntry() override;
  };

  // void mapEntries(const Map::Entries& entries);
  // void arrayEntries(const Array::Strings& strings);

  // void pop(Level& level);
  // void clear();

  static std::string number(double d);
  static std::string quote(absl::string_view str);

  void clear();
  MapPtr makeRootMap();

private:
  friend Level;
  friend Map;
  friend Array;

  void push(Level* level);
  void pop(Level* level);
  void addSanitized(absl::string_view token);
  void addCopy(absl::string_view token);
  void addDouble(double number);
  void addNoCopy(absl::string_view token) { fragments_.push_back(token); }
  void addFragments(const Array::Strings& src);
  void flush();
  Level* topLevel() const { return levels_.top(); }

  Buffer::Instance& response_;
  std::vector<absl::string_view> fragments_;
  std::string buffer_;
  std::stack<Level*> levels_;
};

} // namespace Json
} // namespace Envoy
