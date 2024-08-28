#pragma once

#include <memory>
#include <stack>
#include <string>
#include <type_traits>

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
  static constexpr absl::string_view True = R"(true)";
  static constexpr absl::string_view False = R"(false)";
  static constexpr absl::string_view Null = R"(null)";

  // Constants for JSON delimiters.
  static constexpr absl::string_view MapBeg = R"({)";
  static constexpr absl::string_view MapEnd = R"(})";
  static constexpr absl::string_view ArrayBeg = R"([)";
  static constexpr absl::string_view ArrayEnd = R"(])";
  static constexpr absl::string_view Quote = R"(")";
  static constexpr absl::string_view Comma = R"(,)";
};

class BufferOutput {
public:
  explicit BufferOutput(Buffer::Instance& output) : buffer_(output) {}
  void add(absl::string_view a) { buffer_.addFragments({a}); }
  void add(absl::string_view a, absl::string_view b, absl::string_view c) {
    buffer_.addFragments({a, b, c});
  }

  Buffer::Instance& buffer_;
};

class StringOutput {
public:
  explicit StringOutput(std::string& output) : buffer_(output) {}

  void add(absl::string_view a) { buffer_.append(a); }
  void add(absl::string_view a, absl::string_view b, absl::string_view c) {
    absl::StrAppend(&buffer_, a, b, c);
  }

  std::string& buffer_;
};

/**
 * Provides an API for streaming JSON output, as an alternative to populating a
 * JSON structure with an image of what you want to serialize, or using a
 * protobuf with reflection. The advantage of this approach is that it does not
 * require building an intermediate data structure with redundant copies of all
 * strings, maps, and arrays.
 */
template <class OutputType> class StreamerBase {
public:
  using Value = absl::variant<absl::monostate, absl::string_view, double, uint64_t, int64_t, bool>;

  /**
   * @param output The buffer in which to stream output. Note: this buffer can
   *               be flushed during population; it is not necessary to hold
   *               the entire json structure in memory before streaming it to
   *               the network.
   */
  template <class T> explicit StreamerBase(T& output) : output_(output) {}

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
    Level(StreamerBase& streamer, absl::string_view opener, absl::string_view closer)
        : streamer_(streamer), closer_(closer) {
      streamer_.addDirectly(opener);
#ifndef NDEBUG
      streamer_.push(this);
#endif
    }
    virtual ~Level() {
      streamer_.addDirectly(closer_);
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
     * Serializes a number or string or bool to the output stream. Doubles that are NaN are
     * rendered as 'null'. Strings are sanitized if needed, and surrounded by quotes.
     *
     * @param value the variant value to render.
     */
    void addValue(const Value& value) {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();

      switch (value.index()) {
      case 0:
        static_assert(isSameType<decltype(absl::get<0>(value)), absl::monostate>(),
                      "value at index 0 must be an absl::monostate");
        this->streamer_.addNull();
        break;
      case 1:
        static_assert(isSameType<decltype(absl::get<1>(value)), absl::string_view>(),
                      "value at index 1 must be an absl::string_vlew");
        this->streamer_.addString(absl::get<absl::string_view>(value));
        break;
      case 2:
        static_assert(isSameType<decltype(absl::get<2>(value)), double>(),
                      "value at index 2 must be a double");
        this->streamer_.addNumber(absl::get<double>(value));
        break;
      case 3:
        static_assert(isSameType<decltype(absl::get<3>(value)), uint64_t>(),
                      "value at index 3 must be a uint64_t");
        this->streamer_.addNumber(absl::get<uint64_t>(value));
        break;
      case 4:
        static_assert(isSameType<decltype(absl::get<4>(value)), int64_t>(),
                      "value at index 4 must be an int64_t");
        this->streamer_.addNumber(absl::get<int64_t>(value));
        break;
      case 5:
        static_assert(isSameType<decltype(absl::get<5>(value)), bool>(),
                      "value at index 5 must be a bool");
        this->streamer_.addBool(absl::get<bool>(value));
        break;
      default:
        IS_ENVOY_BUG(absl::StrCat("addValue invalid index: ", value.index()));
        this->streamer_.addNull();
        break;
      }
    }

  protected:
    template <class Type, class Expected> static constexpr bool isSameType() {
      return std::is_same_v<
          typename std::remove_cv<typename std::remove_reference<Type>::type>::type, Expected>;
    }

    /**
     * Initiates a new field, serializing a comma separator if this is not the
     * first one.
     */
    virtual void nextField() {
      if (is_first_) {
        is_first_ = false;
      } else {
        streamer_.addDirectly(Constants::Comma);
      }
    }

    bool is_first_{true}; // Used to control whether a comma-separator is added for a new entry.
    StreamerBase& streamer_;
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

    Map(StreamerBase& streamer) : Level(streamer, Constants::MapBeg, Constants::MapEnd) {}

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
      this->streamer_.addSanitized(Constants::Quote, key, R"(":)");
      expecting_value_ = true;
    }

    /**
     * Populates a list of name/value pairs in a single call. This function
     * makes it easy to populate structures with scalar values. It's a
     * programming error to call this method on a map that's not the current top
     * level.
     */
    void addEntries(Entries entries) {
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
    Array(StreamerBase& streamer) : Level(streamer, Constants::ArrayBeg, Constants::ArrayEnd) {}
    using Entries = absl::Span<const Value>;

    /**
     * Adds values to an array. The values may be numeric or strings; strings
     * will be escaped if needed. It's a programming error to call this method
     * on an array that's not the current top level.
     *
     * @param entries the array of numeric or string values.
     */
    void addEntries(Entries entries) {
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

  /**
   * Takes a raw string, sanitizes it using JSON syntax, surrounds it
   * with a prefix and suffix, and streams it out.
   */
  void addSanitized(absl::string_view prefix, absl::string_view token, absl::string_view suffix) {
    absl::string_view sanitized = Json::sanitize(sanitize_buffer_, token);
    output_.add(prefix, sanitized, suffix);
  }

  /**
   * Serializes a string to the output stream. The string is sanitized if needed, and surrounded
   * by quotes.
   */
  void addString(absl::string_view str) { addSanitized(Constants::Quote, str, Constants::Quote); }
  void addString(const char* str) { addSanitized(Constants::Quote, str, Constants::Quote); }

  /**
   * Serializes a number.
   */
  void addNumber(double d) {
    if (std::isnan(d)) {
      output_.add(Constants::Null);
    } else {
      Buffer::Util::serializeDouble(d, output_);
    }
  }
  void addNumber(uint64_t u) { output_.add(absl::StrCat(u)); }
  void addNumber(int64_t i) { output_.add(absl::StrCat(i)); }

  /**
   * Serializes a bool to the output stream.
   */
  void addBool(bool b) { output_.add(b ? Constants::True : Constants::False); }

  /**
   * Serializes a null to the output stream.
   */
  void addNull() { output_.add(Constants::Null); }

  /**
   * Adds a raw string piece to the output stream. The string must be pre-sanitized and is legal
   * JSON piece.
   * @param str the string to append.
   */
  void addDirectly(absl::string_view str) { output_.add(str); }

private:
  friend Level;
  friend Map;
  friend Array;

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

  OutputType output_;
  std::string sanitize_buffer_;

#ifndef NDEBUG
  // Keeps a stack of Maps or Arrays (subclasses of Level) to facilitate
  // assertions that only the top-level map/array can be written.
  std::stack<Level*> levels_;
#endif
};

using Streamer = StreamerBase<BufferOutput>;

} // namespace Json
} // namespace Envoy
