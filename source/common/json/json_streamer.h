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

/**
 * Provides an API for streaming JSON output, as an alternative to populating a
 * JSON structure with an image of what you want to serialize, or using a
 * protobuf with reflection. The advantage of this approach is that it does not
 * require building an intermediate data structure with redundant copies of all
 * strings, maps, and arrays.
 */
class Streamer {
public:
  /**
   * @param response The buffer in which to stream output. Note: this buffer can
   *                 be flushed during population; it is not necessary to hold
   *                 the entire json structure in memory before streaming it to
   *                 the network.
   */
  explicit Streamer(Buffer::Instance& response) : response_(response) {}

  class Array;
  using ArrayPtr = std::unique_ptr<Array>;
  class Map;
  using MapPtr = std::unique_ptr<Map>;

  /**
   * Represents the current map or array. We keep track of what character is
   * needed to close it, and whether or not the first entry has been added.
   */
  class Level {
  public:
    Level(Streamer& streamer, absl::string_view opener, absl::string_view closer);
    virtual ~Level();

    /**
     * @return a newly created subordinate map.
     */
    MapPtr newMap();

    /**
     * @return a newly created subordinate array.
     */
    ArrayPtr newArray();

    Level* topLevel() const { return streamer_.topLevel(); }

  protected:
    /**
     * Initiates a new entry, serializing a comma separator if this is not
     * the first one.
     */
    virtual void newEntry();

  private:
    friend Streamer;

    /**
     * An aggregate can be closed in two ways. In either case, we need each
     *     aggregate to admit its closing delimiter ("}" or "]").
     *
     *  1. The unique_ptr in which it is allocated is destructed or reset.
     *     This is the normal mode of operation during serialization of a
     *     series of map or array structures. This triggers emitting the
     *     closing delimiter from the destructor.
     *  2. At the end of serialization process a stack of aggregate objects
     *     is unwound via Streamer::close(). When this occurs, we emit the
     *     closing delimiter but need to avoid doing so again when the
     *     object is eventually destroyed.
     */
    void close();

    bool is_closed_{false}; // Used to avoid emitting the closing delimiter twice.
    bool is_first_{true};   // Used to control whether a comma-separator is added for a new entry.
    Streamer& streamer_;
    absl::string_view closer_;
  };
  using LevelPtr = std::unique_ptr<Level>;

  /**
   * Represents a JSON map while it is being serialized. No data is buffered
   * in the structure; just enough state to be able emit delimiters properly.
   */
  class Map : public Level {
    using NameValue = std::pair<const absl::string_view, const absl::string_view>;
    using Entries = absl::Span<const NameValue>;

  public:
    /**
     * Represents a map value which will be closed at a later point. This is
     * needed for event-driven serialization, where we cannot close out a map
     * value prior to returning from the function that emitted the map key.
     *
     * All this structure really needs to do is clear Map::expecting_value_
     * after it is destructed, but there's some complexity required to allow the
     * Map to be destroyed prior to the Value, which can occur during
     * Streamer::close().
     */
    class DeferredValue {
    public:
      explicit DeferredValue(Map& map);
      ~DeferredValue();

    private:
      friend Map;

      /**
       * Called from Map's destructor to ensure that a DeferredValue object
       * does not outlast the Map in which it was instantiated. This is needed
       * when the Map is closed before destruction due to Streamer::close().
       */
      void close();

      Map& map_;
      bool managed_{true};
    };
    using DeferredValuePtr = std::unique_ptr<DeferredValue>;

    Map(Streamer& streamer) : Level(streamer, "{", "}") {}
    ~Map() override;

    /**
     * Initiates a new map key, and runs a supplied function to then
     * emit the value. The supplied function may emit the value in
     * one of three ways:
     *   1. Instantiating and populating a subordinate map or array.
     *   2. Call newSanitizedValue() with a value to be json-sanitized,
     *      quoted, and emitted.
     *   3. Call deferValue() which returns a structure that can be saved
     *      and and resolved at a later time. This makes it possible to
     *      incrementally stream map contents.
     *
     * See also newEntries, which directly populates a list of name/value
     * pairs in a single call.
     */
    void newKey(absl::string_view name, std::function<void()>);

    /**
     * @return a structure to represent the deferred value; to be filled later.
     */
    DeferredValuePtr deferValue();

    /**
     * Populates a list of name/value pairs in a single call. This function
     * makes it easy to populate structures with scalar values.
     */
    void newEntries(const Entries& entries);

    void newSanitizedValue(absl::string_view value);
    void clearDeferredValue() {
      deferred_value_.reset();
      expecting_value_ = false;
    }
    void addSanitized(absl::string_view value);

  protected:
    virtual void newEntry() override;

  private:
    bool expecting_value_{false};
    OptRef<DeferredValue> deferred_value_;
  };

  class Array : public Level {
  public:
    Array(Streamer& streamer) : Level(streamer, "[", "]") {}
    using Strings = absl::Span<const absl::string_view>;
    void newEntries(const Strings& entries);
  };

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
