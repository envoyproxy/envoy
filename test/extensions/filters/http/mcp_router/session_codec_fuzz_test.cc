#include <string>

#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  static constexpr size_t kMaxInputSize = 64 * 1024;
  if (len > kMaxInputSize) {
    return;
  }

  const std::string input(reinterpret_cast<const char*>(buf), len);

  // Primary target: parse arbitrary bytes as a composite session ID.
  auto parsed = SessionCodec::parseCompositeSessionId(input);
  if (parsed.ok()) {
    (void)parsed->route.size();
    (void)parsed->subject.size();
    for (const auto& [backend, session] : parsed->backend_sessions) {
      (void)backend.size();
      (void)session.size();
    }
  }

  // Encode/decode round-trip on raw bytes.
  const std::string encoded = SessionCodec::encode(input);
  (void)SessionCodec::decode(encoded);

  // Build then parse round-trip: catches asymmetries between encoder and parser.
  if (len <= 256) {
    absl::flat_hash_map<std::string, std::string> sessions{{"b", input}};
    const std::string composite = SessionCodec::buildCompositeSessionId("r", input, sessions);
    (void)SessionCodec::parseCompositeSessionId(composite);
  }
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
