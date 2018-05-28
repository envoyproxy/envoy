#include "common/common/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {

  uint64_t out;
  std::string stringBuffer(*buf, len);
  absl::string_view abslString = stringBuffer;

  if (len > 0) {
    Envoy::StringUtil::atoul(stringBuffer.c_str(), out);
    Envoy::StringUtil::escape(stringBuffer);
    Envoy::StringUtil::endsWith(stringBuffer, stringBuffer);
    Envoy::StringUtil::caseCompare(abslString, abslString);
    Envoy::StringUtil::cropLeft(abslString, abslString);
    Envoy::StringUtil::cropRight(abslString, abslString);
    Envoy::StringUtil::toUpper(abslString);
    Envoy::StringUtil::trim(abslString);
    Envoy::StringUtil::ltrim(abslString);
    Envoy::StringUtil::rtrim(abslString);
  }
}

} // namespace Fuzz
} // namespace Envoy
