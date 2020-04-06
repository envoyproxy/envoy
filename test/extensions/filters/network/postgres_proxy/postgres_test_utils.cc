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
  uint32_t msg_length = htonl(4 + payload.length());
  data.add(&msg_length, 4);
  data.add(payload);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
