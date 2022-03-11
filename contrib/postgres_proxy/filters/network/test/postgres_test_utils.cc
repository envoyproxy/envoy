#include "contrib/postgres_proxy/filters/network/test/postgres_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Helper function to create postgres messages.
void createPostgresMsg(Buffer::Instance& data, std::string type, std::string payload) {
  data.drain(data.length());
  ASSERT(1 == type.length());
  data.add(type);
  data.writeBEInt<uint32_t>(4 + (payload.empty() ? 0 : (payload.length() + 1)));
  if (!payload.empty()) {
    data.add(payload);
    data.writeBEInt<uint8_t>(0);
  }
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
