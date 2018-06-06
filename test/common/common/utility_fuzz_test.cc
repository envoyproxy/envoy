#include "common/common/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  {
    uint64_t out;
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    StringUtil::atoul(string_buffer.c_str(), out);
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
      StringUtil::endsWith(string_buffer.substr(0, split_point), string_buffer.substr(split_point));
    }
    {
      const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
      StringUtil::caseCompare(string_buffer.substr(0, split_point),
                              string_buffer.substr(split_point));
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
  }
}

} // namespace Fuzz
} // namespace Envoy
