#include "test/common/json/json_sanitizer_test_util.h"

#include <string>

#include "source/common/common/utility.h"
#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Json {

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

namespace {

class InvalidUnicodeSet {
public:
  InvalidUnicodeSet() {
    // Generated with
    //   bazel build -c opt test/common/json:json_sanitizer_test
    //   GENERATE_INVALID_UTF8_RANGES=1 \
    //     ./bazel-bin/test/common/json/json_sanitizer_test |& \
    //     grep -v 'contains invalid UTF-8'

    invalid_3byte_intervals_.insert(0x17b4, 0x17b6);
    invalid_3byte_intervals_.insert(0x200b, 0x2010);
    invalid_3byte_intervals_.insert(0x2028, 0x202f);
    invalid_3byte_intervals_.insert(0x2060, 0x2065);
    invalid_3byte_intervals_.insert(0x206a, 0x2070);
    invalid_3byte_intervals_.insert(0xd800, 0xe000);
    invalid_3byte_intervals_.insert(0xfeff, 0xff00);
    invalid_3byte_intervals_.insert(0xfff9, 0xfffc);

    invalid_4byte_intervals_.insert(0x1d173, 0x1d17b);
    invalid_4byte_intervals_.insert(0xe0001, 0xe0002);
    invalid_4byte_intervals_.insert(0xe0020, 0xe0080);
    invalid_4byte_intervals_.insert(0x110000, 0x200000);
  }

  bool isValid3Byte(uint32_t unicode) const { return !invalid_3byte_intervals_.test(unicode); }

  bool isValid4Byte(uint32_t unicode) const { return !invalid_4byte_intervals_.test(unicode); }

private:
  IntervalSetImpl<uint32_t> invalid_3byte_intervals_;
  IntervalSetImpl<uint32_t> invalid_4byte_intervals_;
};

const InvalidUnicodeSet& invalidUnicodeSet() { CONSTRUCT_ON_FIRST_USE(InvalidUnicodeSet); }

} // namespace

bool isValidUtf8(absl::string_view in, bool exclude_protobuf_exceptions) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  uint32_t size = in.size();
  while (size != 0) {
    if ((*data & 0x80) == 0) {
      ++data;
      --size;
    } else {
      auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(data, size);
      data += consumed;
      size -= consumed;

      switch (consumed) {
      case 0:
      case 1:
        return false;
      case 2:
        if (unicode < 0x80) { // 2-byte sequences start at 0x80
          return false;
        }
        break;
      case 3:
        if (unicode < 0x800) { // 3-byte starts at 0x800
          // Protobufs reject 12-bit values encoded as 3-byte utf8, as well as
          // a few other code-points.
          return false;
        } else if (exclude_protobuf_exceptions && !invalidUnicodeSet().isValid3Byte(unicode)) {
          return false;
        }
        break;
      case 4:
        if (unicode < 0x10000) { // 4-byte starts at 0x10000
          return false;
        } else if (exclude_protobuf_exceptions && !invalidUnicodeSet().isValid4Byte(unicode)) {
          return false;
        }
        break;
      }
    }
  }
  return true;
}

} // namespace Json
} // namespace Envoy
