#pragma once

#include <memory>
#include <stack>
#include <string>
#include <type_traits>
#include <variant>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_util.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Json {

// To ensure the streamer is being used correctly, we use assertions to enforce
// that only the topmost map/array in the stack is being written to. To make
// this easier to do from the Level classes, we provider Streamer::topLevel() as
// a member function, but this is only needed when compiled for debug.
//
// We only compile Streamer::topLevel in debug to avoid having it be a coverage
// gap. However, assertions fail to compile in release mode if they reference
// non-existent functions or member variables, so we only compile the assertions
// in debug mode.
#ifdef NDEBUG
#define ASSERT_THIS_IS_TOP_LEVEL                                                                   \
  do {                                                                                             \
  } while (0)
#define ASSERT_LEVELS_EMPTY                                                                        \
  do {                                                                                             \
  } while (0)
#else
#define ASSERT_THIS_IS_TOP_LEVEL ASSERT(this->streamer_.topLevel() == this)
#define ASSERT_LEVELS_EMPTY ASSERT(this->levels_.empty())
#endif

class Constants {
public:
  // Constants for common JSON values.
  static constexpr absl::string_view True = "true";
  static constexpr absl::string_view False = "false";
  static constexpr absl::string_view Null = "null";
};

// Simple abstraction that provide a output buffer for streaming JSON output.
class BufferOutput {
public:
  void add(absl::string_view a) { buffer_.addFragments({a}); }
  void add(absl::string_view a, absl::string_view b, absl::string_view c) {
    buffer_.addFragments({a, b, c});
  }

  explicit BufferOutput(Buffer::Instance& output) : buffer_(output) {}
  Buffer::Instance& buffer_;
};

/**
 * Provides an API for streaming JSON output, as an alternative to populating a
 * JSON structure with an image of what you want to serialize, or using a
 * protobuf with reflection. The advantage of this approach is that it does not
 * require building an intermediate data structure with redundant copies of all
 * strings, maps, and arrays.
 */
class Streamer {
public:
  using Value = absl::variant<absl::string_view, double, uint64_t, int64_t, bool>;

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
    Level(Streamer& streamer, absl::string_view opener, absl::string_view closer)
        : streamer_(streamer), closer_(closer) {
      streamer_.addConstantString(opener);
#ifndef NDEBUG
      streamer_.push(this);
#endif
    }
    virtual ~Level() {
      streamer_.addConstantString(closer_);
#ifndef NDEBUG
      streamer_.pop(this);
#endif
    }

    /**
     * This must be called on the top level map or array. It's a programming
     * error to call this method on a map that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     *
     * @return a newly created subordinate map, which becomes the new top level until destroyed.
     */
    MapPtr addMap() {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      return std::make_unique<Map>(streamer_);
    }

    /**
     * This must be called on the top level map or array. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     *
     * @return a newly created subordinate array, which becomes the new top level until destroyed.
     */
    ArrayPtr addArray() {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      return std::make_unique<Array>(streamer_);
    }

    /**
     * Adds a numeric value to the current array or map. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     */
    void addNumber(double number) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addNumber(number);
    }
    void addNumber(uint64_t number) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addNumber(number);
    }
    void addNumber(int64_t number) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addNumber(number);
    }

    /**
     * Adds a string constant value to the current array or map. The string
     * will be sanitized per JSON rules.
     *
     * It's a programming error to call this method on a map or array that's not
     * the top level. It's also a programming error to call this on map that
     * isn't expecting a value. You must call Map::addKey prior to calling this.
     */
    void addString(absl::string_view str) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addString(str);
    }

    /**
     * Adds a bool constant value to the current array or map. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     */
    void addBool(bool b) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addBool(b);
    }

  protected:
    /**
     * Initiates a new field, serializing a comma separator if this is not the
     * first one.
     */
    virtual void nextField() {
      if (is_first_) {
        is_first_ = false;
      } else {
        streamer_.addConstantString(",");
      }
    }

    /**
     * Renders a string or a number in json format. Doubles that are NaN are
     * rendered as 'null'. Strings are json-sanitized if needed, and surrounded
     * by quotes.
     *
     * @param Value the value to render.
     */
    void addValue(const Value& value) {
      static_assert(absl::variant_size_v<Value> == 5, "Value must be a variant with 5 types");

      switch (value.index()) {
      case 0:
        static_assert(std::is_same<decltype(absl::get<0>(value)), const absl::string_view&>::value,
                      "value at index 0 must be an absl::string_vlew");
        addString(absl::get<absl::string_view>(value));
        break;
      case 1:
        static_assert(std::is_same<decltype(absl::get<1>(value)), const double&>::value,
                      "value at index 1 must be a double");
        addNumber(absl::get<double>(value));
        break;
      case 2:
        static_assert(std::is_same<decltype(absl::get<2>(value)), const uint64_t&>::value,
                      "value at index 2 must be a uint64_t");
        addNumber(absl::get<uint64_t>(value));
        break;
      case 3:
        static_assert(std::is_same<decltype(absl::get<3>(value)), const int64_t&>::value,
                      "value at index 3 must be an int64_t");
        addNumber(absl::get<int64_t>(value));
        break;
      case 4:
        static_assert(std::is_same<decltype(absl::get<4>(value)), const bool&>::value,
                      "value at index 4 must be a bool");
        addBool(absl::get<bool>(value));
        break;
      }
    }

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
    void addKey(absl::string_view key) {
      ASSERT_THIS_IS_TOP_LEVEL;
      ASSERT(!expecting_value_);
      nextField();
      this->streamer_.addSanitized("\"", key, "\":");
      expecting_value_ = true;
    }

    /**
     * Populates a list of name/value pairs in a single call. This function
     * makes it easy to populate structures with scalar values. It's a
     * programming error to call this method on a map that's not the current top
     * level.
     */
    void addEntries(const Entries& entries) {
      for (const NameValue& entry : entries) {
        addKey(entry.first);
        this->addValue(entry.second);
      }
    }

  protected:
    void nextField() override {
      if (expecting_value_) {
        expecting_value_ = false;
      } else {
        Level::nextField();
      }
    }

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
    void addEntries(const Entries& entries) {
      for (const Value& value : entries) {
        this->addValue(value);
      }
    }
  };

  /**
   * Makes a root map for the streamer.
   *
   * You must create a root map or array before any of the JSON population
   * functions can be called, as those are only available on Map and Array
   * objects.
   */
  MapPtr makeRootMap() {
    ASSERT_LEVELS_EMPTY;
    return std::make_unique<Map>(*this);
  }

  /**
   * Makes a root array for the streamer.
   *
   * You must create a root map or array before any of the JSON population
   * functions can be called, as those are only available on Map and Array
   * objects.
   */
  ArrayPtr makeRootArray() {
    ASSERT_LEVELS_EMPTY;
    return std::make_unique<Array>(*this);
  }

private:
  friend Level;
  friend Map;
  friend Array;

  /**
   * Takes a raw string, sanitizes it using JSON syntax, surrounds it
   * with a prefix and suffix, and streams it out.
   */
  void addSanitized(absl::string_view prefix, absl::string_view token, absl::string_view suffix) {
    absl::string_view sanitized = Json::sanitize(sanitize_buffer_, token);
    response_.add(prefix, sanitized, suffix);
  }

  void addString(absl::string_view str) {
    response_.add("\"", Json::sanitize(sanitize_buffer_, str), "\"");
  }

  /**
   * Serializes a number.
   */
  void addNumber(double d) {
    if (std::isnan(d)) {
      response_.add(Constants::Null);
    } else {
      Buffer::Util::serializeDouble(d, response_.buffer_);
    }
  }
  void addNumber(uint64_t u) { response_.add(absl::StrCat(u)); }
  void addNumber(int64_t i) { response_.add(absl::StrCat(i)); }

  /**
   * Serializes a bool to the output stream.
   */
  void addBool(bool b) { response_.add(b ? Constants::True : Constants::False); }

  /**
   * Adds a constant string to the output stream. The string must outlive the
   * Streamer object, and is intended for literal strings such as punctuation.
   */
  void addConstantString(absl::string_view str) { response_.add(str); }

#ifndef NDEBUG
  /**
   * @return the top Level*. This is used for asserts.
   */
  Level* topLevel() const { return levels_.top(); }

  /**
   * Pushes a new level onto the stack.
   */
  void push(Level* level) { levels_.push(level); }

  /**
   * Pops a level off of a stack, asserting that it matches.
   */
  void pop(Level* level) {
    ASSERT(levels_.top() == level);
    levels_.pop();
  }

#endif

  BufferOutput response_;
  std::string sanitize_buffer_;

#ifndef NDEBUG
  // Keeps a stack of Maps or Arrays (subclasses of Level) to facilitate
  // assertions that only the top-level map/array can be written.
  std::stack<Level*> levels_;
#endif
};

} // namespace Json
} // namespace Envoy
