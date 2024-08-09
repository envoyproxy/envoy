#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"
#include "json_internal.h"

namespace Envoy {
namespace Json {

using BufferWriter = Envoy::Buffer::Instance;

class StringWriter {
public:
  StringWriter(size_t initial_buffer_size = 2048) { buffer_.reserve(initial_buffer_size); }

  void addFragments(absl::Span<const absl::string_view> fragments) {
    for (absl::string_view fragment : fragments) {
      buffer_.append(fragment.data(), fragment.size());
    }
  }
  std::string buffer_;
};

/**
 * Helper class to sanitize keys, values, and delimiters to short JSON pieces
 * (strings).
 * NOTE: Use this class carefully. YOURSELF should make sure the JSON pieces
 * are sanitized and constructed correctly.
 * NOTE: Although this class could but is not designed to construct a complete
 * JSON output. It is designed to sanitize and construct partial JSON pieces
 * on demand.
 * NOTE: If a complete JSON output is needed, using Envoy::Json::Streamer first
 * except YOU KNOW WHAT YOU ARE DOING.
 * NOTE: Serializer does exactly the same thing as Streamer, except using strings.
 * Choose which to use based on context.
 */
template <class Writer> class Serializer {
public:
  static constexpr absl::string_view QuoteValue = R"(")";
  static constexpr absl::string_view TrueValue = R"(true)";
  static constexpr absl::string_view FalseValue = R"(false)";
  static constexpr absl::string_view NullValue = R"(null)";

  // Constructor with writer to write the JSON pieces.
  Serializer(Writer& writer) : writer_(writer) {}

  /**
   * Add delimiter '{' to the raw JSON piece buffer.
   */
  void addMapBegDelimiter() { writer_.addFragments({"{"}); }

  /**
   * Add delimiter '}' to the raw JSON piece buffer.
   */
  void addMapEndDelimiter() { writer_.addFragments({"}"}); }

  /**
   * Add delimiter '[' to the raw JSON piece buffer.
   */
  void addArrayBegDelimiter() { writer_.addFragments({"["}); }

  /**
   * Add delimiter ']' to the raw JSON piece buffer.
   */
  void addArrayEndDelimiter() { writer_.addFragments({"]"}); }

  /**
   * Add delimiter ':' to the raw JSON piece buffer.
   */
  void addKeyValueDelimiter() { writer_.addFragments({":"}); }

  /**
   * Add delimiter ',' to the raw JSON piece buffer.
   */
  void addElementsDelimiter() { writer_.addFragments({","}); }

  /**
   * Add a string value to the raw JSON piece buffer. The string value will
   * be sanitized per JSON rules.
   *
   * @param value The string value or key to be sanitized and added.
   * @param QUOTE Whether to quote the string value. Default is true. This
   * parameter should be true unless the caller wants to handle the quoting
   * by itself.
   *
   * NOTE: Both key and string values should use this method to sanitize.
   */
  template <bool QUOTE = true> void addString(absl::string_view value) {
    // Sanitize the string value and quote it on demand of the caller.
    addPiece<QUOTE>(Utility::escape(value).toStringView());
  }

  /**
   * Add a number value to the raw JSON piece buffer.

   * @param value The number value to be added.
   * @param QUOTE Whether to quote the number value. Default is false. This
   * parameter should be false unless the caller wants to serialize the
   * number value as a string.
   */
  template <bool QUOTE = false> void addNumber(double value) {
    // TODO(wbpcode): use the Buffer::Util::serializeDouble function to serialize the
    // double value.
    addPiece<QUOTE>(fmt::to_string(value));
  }

  /**
   * Add a bool value to the raw JSON piece buffer.
   *
   * @param value The bool value to be added.
   * @param QUOTE Whether to quote the bool value. Default is false. This
   * parameter should be false unless the caller wants to serialize the
   * bool value as a string.
   */
  template <bool QUOTE = false> void addBool(bool value) {
    addPiece<QUOTE>(value ? TrueValue : FalseValue);
  }

  /**
   * Add a null value to the raw JSON piece buffer.
   */
  void addNull() { addPiece<false>(NullValue); }

private:
  /**
   * Add a raw string piece to the buffer. Please make sure the string piece
   * is sanitized before calling this method.
   * The string piece may represent a JSON key, string value, number value,
   * delimiter, or even a complete/partial JSON piece.
   *
   * @param sanitized_piece The sanitized string piece to be added.
   * @param QUOTE is used to control whether to quote the input string piece.
   * Default is false.
   *
   * NOTE: This method should be used carefully. The caller should make sure
   * the input string piece is sanitized and constructed correctly and the
   * quote parameter is set correctly.
   */
  template <bool QUOTE = false> void addPiece(absl::string_view sanitized_piece) {
    if constexpr (QUOTE) {
      writer_.addFragments({QuoteValue, sanitized_piece, QuoteValue});
    } else {
      writer_.addFragments({sanitized_piece});
    }
  }

  Writer& writer_;
};

} // namespace Json
} // namespace Envoy
