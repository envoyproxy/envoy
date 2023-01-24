#include "contrib/smtp_proxy/filters/network/test/smtp_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Helper function to create smtp messages.
void createSmtpMsg(Buffer::Instance& data, std::string type, std::string payload) {
  data.drain(data.length());
  ASSERT(1 == type.length());
  data.add(type);
  data.writeBEInt<uint32_t>(4 + (payload.empty() ? 0 : (payload.length() + 1)));
  if (!payload.empty()) {
    data.add(payload);
    data.writeBEInt<uint8_t>(0);
  }
}

// Helper function to create an initial smtp message.
void createInitialSmtpRequest(Buffer::Instance& data) {
  // Startup message has the following structure:
  // Length (4 bytes) - payload and length field
  // version (4 bytes)
  // Attributes: key/value pairs separated by '\0'
  data.writeBEInt<uint32_t>(33);
  // Add version code
  data.writeBEInt<uint32_t>(0x00030000);
  // user-smtp key-pair
  data.add("user"); // 4 bytes
  data.writeBEInt<uint8_t>(0);
  data.add("smtp"); // 4 bytes
  data.writeBEInt<uint8_t>(0);
  // database-test-db key-pair
  // Some other attribute
  data.add("attribute"); // 9 bytes
  data.writeBEInt<uint8_t>(0);
  data.add("blah"); // 4 bytes
  data.writeBEInt<uint8_t>(0);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
