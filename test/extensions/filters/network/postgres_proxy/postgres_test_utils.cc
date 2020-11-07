#include "test/extensions/filters/network/postgres_proxy/postgres_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Helper function to create postgres messages.
void createPostgresMsg(Buffer::Instance& data, std::string type, std::string payload) {
  data.drain(data.length());
  ASSERT(1 == type.length());
  data.add(type);
  data.writeBEInt<uint32_t>(4 + payload.length());
  data.add(payload);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
