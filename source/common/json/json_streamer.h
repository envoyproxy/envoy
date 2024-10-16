#pragma once

#include <memory>
#include <stack>
#include <string>
#include <type_traits>
#include <variant>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_util.h"
#include "source/common/json/constants.h"
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

// Buffer wrapper that implements the necessary abstraction for the template
// StreamerBase.
// This could be used to stream JSON output of StreamerBase to a Buffer.
class BufferOutput {
public:
  void add(absl::string_view a) { buffer_.addFragments({a}); }
  void add(absl::string_view a, absl::string_view b, absl::string_view c) {
    buffer_.addFragments({a, b, c});
  }

  explicit BufferOutput(Buffer::Instance& output) : buffer_(output) {}
  Buffer::Instance& buffer_;
};

// String wrapper that implements the necessary abstraction for the template
// StreamerBase.
// This could be used to stream JSON output of StreamerBase to a single string.
class StringOutput {
public:
  void add(absl::string_view a) { buffer_.append(a); }
  void add(absl::string_view a, absl::string_view b, absl::string_view c) {
    absl::StrAppend(&buffer_, a, b, c);
  }
  explicit StringOutput(std::string& output) : buffer_(output) {}

  std::string& buffer_;
};

// Helper class to write value to output buffer in JSON style. This class
// provides a low-level API to operate on the output buffer.
template <class OutputBufferType> class SerializerBase {
public:
  template <class T> explicit SerializerBase(T& output) : output_buffer_(output) {}

  // Methods that be used to add JSON delimiter to output buffer.
  void addMapBeginDelimiter() { output_buffer_.add(Json::Constants::MapBegin); }
  void addMapEndDelimiter() { output_buffer_.add(Json::Constants::MapEnd); }
  void addArrayBeginDelimiter() { output_buffer_.add(Json::Constants::ArrayBegin); }
  void addArrayEndDelimiter() { output_buffer_.add(Json::Constants::ArrayEnd); }
  void addElementsDelimiter() { output_buffer_.add(Json::Constants::Comma); }
  void addKeyValueDelimiter() { output_buffer_.add(Json::Constants::Colon); }

  // Methods that be used to add JSON key or value to output buffer.
  void addString(absl::string_view value) { addSanitized(R"(")", value, R"(")"); }

  /**
   * Serializes a number.
   */
  void addNumber(double d) {
    if (std::isnan(d)) {
      output_buffer_.add(Json::Constants::Null);
    } else {
      Buffer::Util::serializeDouble(d, output_buffer_);
    }
  }
  /**
   * Serializes a integer number.
   * NOTE: All numbers in JSON is float. When loading output of this serializer, the parser's
   * implementation decides if the full precision of big integer could be preserved or not.
   * See discussion here https://stackoverflow.com/questions/13502398/json-integers-limit-on-size
   * and spec https://www.rfc-editor.org/rfc/rfc7159#section-6 for more details.
   */
  void addNumber(uint64_t i) { output_buffer_.add(absl::StrCat(i)); }
  void addNumber(int64_t i) { output_buffer_.add(absl::StrCat(i)); }

  /**
   * Serializes a boolean.
   */
  void addBool(bool b) { output_buffer_.add(b ? Json::Constants::True : Json::Constants::False); }

  /**
   * Serializes a null.
   */
  void addNull() { output_buffer_.add(Json::Constants::Null); }

  // Low-level methods that be used to provide a low-level control to buffer.
  void addSanitized(absl::string_view prefix, absl::string_view value, absl::string_view suffix) {
    output_buffer_.add(prefix, Json::sanitize(sanitize_buffer_, value), suffix);
  }

protected:
  std::string sanitize_buffer_;
  OutputBufferType output_buffer_;
};

/**
 * Provides an API for streaming JSON output, as an alternative to populating a
 * JSON structure with an image of what you want to serialize, or using a
 * protobuf with reflection. The advantage of this approach is that it does not
 * require building an intermediate data structure with redundant copies of all
 * strings, maps, and arrays.
 *
 * NOTE: This template take a type that can be used to stream output. This is either
 * BufferOutput, StringOutput or any other types that have implemented
 * add(absl::string_view) and
 * add(absl::string_view, absl::string_view, absl::string_view) methods.
 */
template <class OutputBufferType> class StreamerBase : public SerializerBase<OutputBufferType> {
public:
  using Value = absl::variant<absl::string_view, double, uint64_t, int64_t, bool, absl::monostate>;

  /**
   * @param response The buffer in which to stream output.
   * NOTE: The response must could be used to construct instance of OutputBufferType.
   */
  template <class T>
  explicit StreamerBase(T& response) : SerializerBase<OutputBufferType>(response) {}

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
    Level(StreamerBase& streamer) : streamer_(streamer) {
#ifndef NDEBUG
      streamer_.push(this);
#endif
    }
    virtual ~Level() {
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

    /**
     * Adds a null constant value to the current array or map. It's a programming
     * error to call this method on a map or array that's not the top level.
     * It's also a programming error to call this on map that isn't expecting
     * a value. You must call Map::addKey prior to calling this.
     */
    void addNull() {
      ASSERT_THIS_IS_TOP_LEVEL;
      nextField();
      streamer_.addNull();
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
        streamer_.addElementsDelimiter();
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
      static_assert(absl::variant_size_v<Value> == 6, "Value must be a variant with 6 types");

      switch (value.index()) {
      case 0:
        static_assert(std::is_same_v<absl::variant_alternative_t<0, Value>, absl::string_view>,
                      "value at index 0 must be an absl::string_vlew");
        addString(absl::get<absl::string_view>(value));
        break;
      case 1:
        static_assert(std::is_same_v<absl::variant_alternative_t<1, Value>, double>,
                      "value at index 1 must be a double");
        addNumber(absl::get<double>(value));
        break;
      case 2:
        static_assert(std::is_same_v<absl::variant_alternative_t<2, Value>, uint64_t>,
                      "value at index 2 must be a uint64_t");
        addNumber(absl::get<uint64_t>(value));
        break;
      case 3:
        static_assert(std::is_same_v<absl::variant_alternative_t<3, Value>, int64_t>,
                      "value at index 3 must be an int64_t");
        addNumber(absl::get<int64_t>(value));
        break;
      case 4:
        static_assert(std::is_same_v<absl::variant_alternative_t<4, Value>, bool>,
                      "value at index 4 must be a bool");
        addBool(absl::get<bool>(value));
        break;
      case 5:
        static_assert(std::is_same_v<absl::variant_alternative_t<5, Value>, absl::monostate>,
                      "value at index 5 must be an absl::monostate");
        addNull();
        break;
      }
    }

  protected:
    bool is_first_{true}; // Used to control whether a comma-separator is added for a new entry.
    StreamerBase& streamer_;
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

    Map(StreamerBase& streamer) : Level(streamer) { this->streamer_.addMapBeginDelimiter(); }
    ~Map() override { this->streamer_.addMapEndDelimiter(); }

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
    Array(StreamerBase& streamer) : Level(streamer) { this->streamer_.addArrayBeginDelimiter(); }
    ~Array() override { this->streamer_.addArrayEndDelimiter(); }

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

  // Keeps a stack of Maps or Arrays (subclasses of Level) to facilitate
  // assertions that only the top-level map/array can be written.
  std::stack<Level*> levels_;
#endif
};

/**
 * A Streamer that streams to a Buffer::Instance.
 */
using BufferStreamer = StreamerBase<BufferOutput>;

/**
 * A Streamer that streams to a string.
 */
using StringStreamer = StreamerBase<StringOutput>;

} // namespace Json
} // namespace Envoy
