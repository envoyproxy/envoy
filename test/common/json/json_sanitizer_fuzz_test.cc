#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);
  std::string buffer1, buffer2, errmsg;
  while (provider.remaining_bytes() != 0) {
    std::string input = provider.ConsumeRandomLengthString(provider.remaining_bytes());
    absl::string_view sanitized = Envoy::Json::sanitize(buffer1, input);

    // If the input is valid UTF-8 we can do a differential test against the
    // Protobuf JSON sanitizer. Otherwise we are simply ensuring that the
    // sanitizer does not crash.
    if (Envoy::Json::TestUtil::isProtoSerializableUtf8(input)) {
      buffer2 =
          MessageUtil::getJsonStringFromMessageOrError(ValueUtil::stringValue(input), false, true);
      absl::string_view proto_sanitized = Envoy::Json::stripDoubleQuotes(buffer2);
      RELEASE_ASSERT(Envoy::Json::TestUtil::utf8Equivalent(sanitized, proto_sanitized, errmsg),
                     errmsg);
    } else {
      std::string decoded, errmsg;
      EXPECT_TRUE(Json::TestUtil::decodeEscapedJson(sanitized, decoded, errmsg))
          << input << ": " << errmsg;
      EXPECT_EQ(input, decoded);
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
