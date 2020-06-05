#include "common/common/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  {
    uint64_t out;
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::atoull(string_buffer.c_str(), out);
  }
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::escape(string_buffer);
  }
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::toUpper(string_buffer);
  }
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::trim(string_buffer);
  }
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::ltrim(string_buffer);
  }
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::rtrim(string_buffer);
  }
  if (len > 0) {
    const size_t split_point = *reinterpret_cast<const uint8_t*>(buf) % len;
    //  (string_buffer.substr(0, split_point), string_buffer.substr(split_point))
    //  @param1: substring of buffer from beginning to split_point
    //  @param2: substring of buffer from split_point to end of the string
    {
      const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
      absl::EndsWith(string_buffer.substr(0, split_point), string_buffer.substr(split_point));
    }
    {
      const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
      StringUtil::cropLeft(string_buffer.substr(0, split_point), string_buffer.substr(split_point));
    }
    {
      const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
      StringUtil::cropRight(string_buffer.substr(0, split_point),
                            string_buffer.substr(split_point));
    }
    {
      const std::string string_buffer(reinterpret_cast<const char*>(buf), len);

      // sample random bit to use as the whitespace flag
      bool trimWhitespace = split_point & 1;
      const size_t split_point2 =
          len > 1 ? reinterpret_cast<const uint8_t*>(buf)[1] % len : split_point;
      const size_t split1 = std::min(split_point, split_point2);
      const size_t split2 = std::max(split_point, split_point2);

      StringUtil::findToken(string_buffer.substr(0, split1),
                            string_buffer.substr(split1, split2 - split2),
                            string_buffer.substr(split2), trimWhitespace);
    }
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
