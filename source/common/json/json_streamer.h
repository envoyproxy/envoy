#pragma once

#include <memory>
#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

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
  using Value = absl::variant<absl::string_view, double>;

  /*
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
    MapPtr addMap();

    /**
     * @return a newly created subordinate array.
     */
    ArrayPtr addArray();

    void addNumber(double d);
    void addString(absl::string_view str);
    void addStringNoFlush(absl::string_view str);

  protected:
    /**
     * Initiates a new entry, serializing a comma separator if this is not the
     * first one.
     */
    virtual void newEntry();

    /**
     * Renders a string or a double in json format. Doubles that are NaN are
     * rendered as 'null'. Strings are json-sanitized if needed, and surrounded
     * by quotes.
     *
     * @param Value the value to render.
     * @return true if the value was a string, thus requiring a flush() before
     *              returning control to the streamer client, which may mutate
     *              the string.
     */
    bool renderValueNoFlush(const Value& value);

  private:
    friend Streamer;

    /**
     * An aggregate can be closed in two ways. In either case, we need each
     *     aggregate to emit its closing delimiter ("}" or "]").
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
  public:
    using NameValue = std::pair<const absl::string_view, Value>;
    using Entries = absl::Span<const NameValue>;

    Map(Streamer& streamer) : Level(streamer, "{", "}") {}

    /**
     * Initiates a new map key. This must be followed by rendering a value,
     * sub-array, or sub-map. It is a programming error to delete a map that
     * has rendered a key without a matching value.
     *
     * See also addEntries, which directly populates a list of name/value
     * pairs in a single call.
     */
    void newKey(absl::string_view name);

    /**
     * Populates a list of name/value pairs in a single call. This function
     * makes it easy to populate structures with scalar values.
     */
    void addEntries(const Entries& entries);

  protected:
    virtual void newEntry() override;

  private:
    bool expecting_value_{false};
  };

  /**
   * Represents a JSON array while it is being serialized. No data is buffered
   * in the structure; just enough state to be able emit delimiters properly.
   */
  class Array : public Level {
  public:
    Array(Streamer& streamer) : Level(streamer, "[", "]") {}
    using Entries = absl::Span<const Value>;

    /**
     * Renders an array of entry strings as strings. The strings
     * are rendered directly; quotes are not added by this function.
     * So if you want a JSON string you must add quotes yourself
     * prior to calling this.
     *
     * @param entries the array of string entries.
     */
    void addEntries(const Entries& entries);
  };

  /**
   * Unwinds the stack of levels, properlying closing all of them
   * using the appropriate delimiters.
   */
  void clear();

  /**
   * Makes a root map for the streamer. A similar function can be added
   * easily if this class is to be used for a JSON structure with a
   * top-level array.
   */
  MapPtr makeRootMap();

private:
  friend Level;
  friend Map;
  friend Array;

  /**
   * Pushes a new level onto the stack.
   */
  void push(Level* level);

  /**
   * Pops a level off of a stack, asserting that it matches.
   */
  void pop(Level* level);

  /**
   * Takes a raw string, sanitizes it using JSON syntax, adds quotes,
   * and streams it out.
   *
   * After calling this, and before returning control to the streamer's client,
   * Streamer::flush() must be called.
   */
  void addSanitized(absl::string_view token, absl::string_view suffix = "\"");

  /**
   * Serializes a number.
   */
  void addNumber(double d);

  /**
   * Flushes out any pending fragments.
   */
  void flush();

  /**
   * After calling this, and before returning control to the streamer's client,
   * Streamer::flush() must be called.
   */
  void addConstantString(absl::string_view str) { fragments_.push_back(str); }

  /**
   * Advance to the next buffer, flushing to response_ when we run out of buffers.
   */
  void nextBuffer();

#ifndef NDEBUG
  /**
   * @return the top Level*. This is used for asserts.
   */
  Level* topLevel() const { return levels_.top(); }
#endif

  Buffer::Instance& response_;
  std::vector<absl::string_view> fragments_;
  static constexpr uint32_t NumBuffers = 10;
  std::string buffers_[NumBuffers];
  uint32_t buffers_index_{0};
  std::stack<Level*> levels_;
};

} // namespace Json
} // namespace Envoy
