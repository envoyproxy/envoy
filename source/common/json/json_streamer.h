#pragma once

#include <memory>
#include <stack>
#include <string>

#include "envoy/buffer/buffer.h"

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
  using Value = absl::variant<absl::string_view, double, uint64_t, int64_t>;

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
     * This must be called on the top level map or array. It's a programming
     * error to call this method on a map that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     *
     * @return a newly created subordinate map, which becomes the new top level until destroyed.
     */
    MapPtr addMap();

    /**
     * This must be called on the top level map or array. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     *
     * @return a newly created subordinate array, which becomes the new top level until destroyed.
     */
    ArrayPtr addArray();

    /**
     * Adds a numeric value to the current array or map. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     */
    void addNumber(double d);
    void addNumber(uint64_t u);
    void addNumber(int64_t i);

    /**
     * Adds a string constant value to the current array or map. The string
     * will be sanitized per JSON rules.
     *
     * It's a programming error to call this method on a map or array that's not
     * the top level. It's also a programming error to call this on map that
     * isn't expecting a value. You must call Map::addKey prior to calling this.
     */
    void addString(absl::string_view str);

  protected:
    /**
     * Initiates a new field, serializing a comma separator if this is not the
     * first one.
     */
    virtual void nextField();

    /**
     * Renders a string or a number in json format. Doubles that are NaN are
     * rendered as 'null'. Strings are json-sanitized if needed, and surrounded
     * by quotes.
     *
     * @param Value the value to render.
     */
    void addValue(const Value& value);

  private:
    friend Streamer;

    bool is_first_{true}; // Used to control whether a comma-separator is added for a new entry.
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
     * sub-array, or sub-map. It is a programming error to delete a map that has
     * rendered a key without a matching value. It's also a programming error to
     * call this method on a map that's not the current top level.
     *
     * See also addEntries, which directly populates a list of name/value
     * pairs in a single call.
     */
    void addKey(absl::string_view key);

    /**
     * Populates a list of name/value pairs in a single call. This function
     * makes it easy to populate structures with scalar values. It's a
     * programming error to call this method on a map that's not the current top
     * level.
     */
    void addEntries(const Entries& entries);

  protected:
    void nextField() override;

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
     * Adds values to an array. The values may be numeric or strings; strings
     * will be escaped if needed. It's a programming error to call this method
     * on an array that's not the current top level.
     *
     * @param entries the array of numeric or string values.
     */
    void addEntries(const Entries& entries);
  };

  /**
   * Makes a root map for the streamer.
   *
   * You must create a root map or array before any of the JSON population
   * functions can be called, as those are only available on Map and Array
   * objects.
   */
  MapPtr makeRootMap();

  /**
   * Makes a root array for the streamer.
   *
   * You must create a root map or array before any of the JSON population
   * functions can be called, as those are only available on Map and Array
   * objects.
   */
  ArrayPtr makeRootArray();

private:
  friend Level;
  friend Map;
  friend Array;

  /**
   * Takes a raw string, sanitizes it using JSON syntax, surrounds it
   * with a prefix and suffix, and streams it out.
   */
  void addSanitized(absl::string_view prefix, absl::string_view token, absl::string_view suffix);

  /**
   * Serializes a number.
   */
  void addNumber(double d);
  void addNumber(uint64_t u);
  void addNumber(int64_t i);

  /**
   * Flushes out any pending fragments.
   */
  void flush();

  /**
   * Adds a constant string to the output stream. The string must outlive the
   * Streamer object, and is intended for literal strings such as punctuation.
   */
  void addConstantString(absl::string_view str) { response_.addFragments({str}); }

#ifndef NDEBUG
  /**
   * @return the top Level*. This is used for asserts.
   */
  Level* topLevel() const { return levels_.top(); }

  /**
   * Pushes a new level onto the stack.
   */
  void push(Level* level);

  /**
   * Pops a level off of a stack, asserting that it matches.
   */
  void pop(Level* level);
#endif

  Buffer::Instance& response_;
  std::string sanitize_buffer_;

#ifndef NDEBUG
  // Keeps a stack of Maps or Arrays (subclasses of Level) to facilitate
  // assertions that only the top-level map/array can be written.
  std::stack<Level*> levels_;
#endif
};

} // namespace Json
} // namespace Envoy
