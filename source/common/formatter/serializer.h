#pragma once

#include <cmath>
#include <string>

#include "envoy/formatter/substitution_formatter.h"

#include "source/common/buffer/buffer_util.h"
#include "source/common/json/constants.h"
#include "source/common/json/json_sanitizer.h"
#include "source/common/json/json_streamer.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Formatter {

// Helper class to write value to output buffer in JSON style.
// NOTE: This helper class has duplicated logic with the Json::BufferStreamer class but
// provides lower level of APIs to operate on the output buffer (like control the
// delimiters). This is designed for special scenario of substitution formatter and
// is not intended to be used by other parts of the code.
class JsonStringSerializer {
public:
  using OutputBufferType = Json::StringOutput;
  explicit JsonStringSerializer(std::string& output_buffer) : output_buffer_(output_buffer) {}

  // Methods that be used to add JSON delimiter to output buffer.
  void addMapBeginDelimiter() { output_buffer_.add(Json::Constants::MapBegin); }
  void addMapEndDelimiter() { output_buffer_.add(Json::Constants::MapEnd); }
  void addArrayBeginDelimiter() { output_buffer_.add(Json::Constants::ArrayBegin); }
  void addArrayEndDelimiter() { output_buffer_.add(Json::Constants::ArrayEnd); }
  void addElementsDelimiter() { output_buffer_.add(Json::Constants::Comma); }
  void addKeyValueDelimiter() { output_buffer_.add(Json::Constants::Colon); }
  void addStringBeginDelimiter() { output_buffer_.add(R"(")"); }
  void addStringEndDelimiter() { output_buffer_.add(R"(")"); }

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
  void addBool(bool b) { output_buffer_.add(b ? Json::Constants::True : Json::Constants::False); }
  void addNull() { output_buffer_.add(Json::Constants::Null); }

  // Low-level methods that be used to provide a low-level control to buffer.
  void addSanitized(absl::string_view prefix, absl::string_view value, absl::string_view suffix) {
    output_buffer_.add(prefix, Json::sanitize(sanitize_buffer_, value), suffix);
  }
  void addSanitized(absl::string_view value) {
    output_buffer_.add(Json::sanitize(sanitize_buffer_, value));
  }
  void addRawString(absl::string_view value) { output_buffer_.add(value); }

  /**
   * @return the underlying output buffer. This could be used by the helpers that write the
   * JSON output to a string directly.
   */
  std::string& outputBuffer() { return output_buffer_.buffer_; }

protected:
  std::string sanitize_buffer_;
  OutputBufferType output_buffer_;
};

} // namespace Formatter
} // namespace Envoy
