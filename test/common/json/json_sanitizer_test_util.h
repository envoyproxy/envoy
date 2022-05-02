#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace TestUtil {

/**
 * Determines whether the input string can be serialized by protobufs. This is
 * used for testing, to avoid trying to do differentials against Protobuf JSON
 * sanitization, which produces noisy error messages and empty strings when
 * presented with some utf8 sequences that are valid according to spec.
 *
 * @param in the string to validate as utf-8.
 */
bool isProtoSerializableUtf8(absl::string_view in);

bool utf8Equivalent(absl::string_view a, absl::string_view b, std::string& errmsg);
#define EXPECT_UTF8_EQ(a, b, context)                                                              \
  {                                                                                                \
    std::string errmsg;                                                                            \
    EXPECT_TRUE(TestUtil::utf8Equivalent(a, b, errmsg)) << context << "\n" << errmsg;              \
  }

} // namespace TestUtil
} // namespace Json
} // namespace Envoy
