#include "source/common/json/json_sanitizer.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/json/json_internal.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

absl::string_view sanitize(std::string& buffer, absl::string_view str) {
  // Fast-path to see whether any escapes or utf-encoding are needed. If str has
  // only unescaped ascii characters, we can simply return it.
  //
  // Benchmarks show it's faster to just rip through the string with no
  // conditionals, so we only check the arithmetically ORed condition after the
  // loop. This avoids branches and allows simpler loop unrolling by the
  // compiler.
  const bool needs_slow_sanitizer = Utility::requireEscaping(str);
  if (!needs_slow_sanitizer) {
    return str;
  }

  TRY_ASSERT_MAIN_THREAD {
    // The Nlohmann JSON library supports serialization and is not too slow. A
    // hand-rolled sanitizer can be a little over 2x faster at the cost of added
    // production complexity. The main drawback is that this code cannot be used
    // in the data plane as it throws exceptions. Should this become an issue,
    // #20428 can be revived which is faster and doesn't throw exceptions, but
    // adds complexity to the production code base.
    buffer = Nlohmann::Factory::serialize(str, false);
    return stripDoubleQuotes(buffer);
  }
  END_TRY
  catch (std::exception&) {
    // If Nlohmann throws an error, emit an octal escape for any character
    // requiring it. This can occur for invalid utf-8 sequences, and we don't
    // want to crash the server if such a sequence makes its way into a string
    // we need to serialize. For example, if admin endpoint /stats?format=json
    // is called, and a stat name was synthesized from dynamic content such as a
    // gRPC method.
    buffer.clear();
    for (char c : str) {
      if (Utility::requireEscaping(c)) {
        buffer.append(absl::StrFormat("\\%03o", c));
      } else {
        buffer.append(1, c);
      }
    }
  }

  return buffer;
}

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    str = str.substr(1, str.size() - 2);
  } else {
    ASSERT(false,
           absl::StrCat("stripDoubleQuotes called on a str that lacks double-quotes: ", str));
  }
  return str;
}

} // namespace Json
} // namespace Envoy
