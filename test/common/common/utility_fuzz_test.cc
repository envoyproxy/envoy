#include "common/common/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  uint64_t out;
  /**
   * @param string_buffer.substr(len / 2, len / 2) denotes the part from half of the buffer till the
   * end.
   */
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::atoul(string_buffer.c_str(), out);
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::escape(string_buffer);
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::endsWith(string_buffer, string_buffer.substr(len / 2, len / 2));
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::caseCompare(string_buffer.substr(0, len / 2),
                                   string_buffer.substr(len / 2, len / 2));
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::caseCompare(string_buffer.substr(0, len / 2),
                                   string_buffer.substr(len / 2, len / 2));
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::caseCompare(string_buffer.substr(0, len / 2),
                                   string_buffer.substr(len / 2, len / 2));
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::toUpper(string_buffer);
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::trim(string_buffer);
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::ltrim(string_buffer);
  }
  {
    std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    Envoy::StringUtil::rtrim(string_buffer);
  }
}

} // namespace Fuzz
} // namespace Envoy
