#include "source/common/json/json_sanitizer.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/json/json_internal.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

static constexpr bool needs_slow_sanitizer[256] = {
  // clang-format off

  // Control-characters 0-31 all require escapes.
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,

  // Pass through printable characters starting with space. Double-quote and
  // backslash require an escape.
  false, false, true /* " */, false, false, false, false, false,         //  !"#$%&'
  false, false, false, false, false, false, false, false,                // ()*+,-.7
  false, false, false, false, false, false, false, false,                // 01234567
  false, false, false, false, false, false, false, false,                // 89:;<=>?
  false, false, false, false, false, false, false, false,                // @ABCDEFG
  false, false, false, false, false, false, false, false,                // HIJKLMNO
  false, false, false, false, false, false, false, false,                // PQRSTUVW
  false, false, false, false, true /* backslash */, false, false, false, // XYZ[\]^_
  false, false, false, false, false, false, false, false,                // `abcdefg
  false, false, false, false, false, false, false, false,                // hijklmno
  false, false, false, false, false, false, false, false,                // pqrstuvw
  false, false, false, false, false, false, false, true,                 // xyz{|}~\177

  // 0x80-0xff, all of which require calling the slow sanitizer.
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
  true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,

  // clang-format on
};

absl::string_view sanitize(std::string& buffer, absl::string_view str) {
  // Fast-path to see whether any escapes or utf-encoding are needed. If str has
  // only unescaped ascii characters, we can simply return it.
  //
  // Benchmarks show it's faster to just rip through the string with no
  // conditionals, so we only check the arithmetically ORed condition after the
  // loop. This avoids branches and allows simpler loop unrolling by the
  // compiler.
  ASSERT(ARRAY_SIZE(needs_slow_sanitizer) == 256);
  uint32_t need_slow = false;
  for (char c : str) {
    // We need to escape control characters, characters >= 127, and double-quote
    // and backslash.
    need_slow |= needs_slow_sanitizer[static_cast<uint8_t>(c)];
  }
  if (!need_slow) {
    return str; // Fast path, should be executed most of the time (modulo anglo bias).
  }
  TRY_ASSERT_MAIN_THREAD {
    // The Nlohmann JSON library supports serialization and is not too slow. A
    // hand-rolled sanitizer can be a little over 2x faster at the cost of added
    // production complexity. The main drawback is that this code cannot be used
    // in the data plane as it throws exceptions. Should this become an issue,
    // #20428 can be revived which is faster and doesn't throw exceptions, but
    // adds complexity to the production code base.
    buffer = Nlohmann::Factory::serialize(str);
  }
  END_TRY
  catch (std::exception) {
    // If Nlohmann throws an error, emit an octal escape for any character
    // requireing it.
    buffer.clear();
    for (char c : str) {
      if (needs_slow_sanitizer[static_cast<uint8_t>(c)]) {
        buffer.append(absl::StrFormat("\\%03o", c));
      } else {
        buffer.append(1, c);
      }
    }
  }
  if (buffer.size() >= 2 && buffer[0] == '"' && buffer[buffer.size() - 1] == '"') {
    return absl::string_view(buffer).substr(1, buffer.size() - 2);
  }

  return buffer;
}

} // namespace Json
} // namespace Envoy
