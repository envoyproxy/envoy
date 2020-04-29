#include "dns_filter_test_utils.h"

#include "common/runtime/runtime_impl.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

std::string buildQueryForDomain(const std::string& name, uint16_t rec_type, uint16_t rec_class) {
  Runtime::RandomGeneratorImpl random_;
  struct DnsMessageParser::DnsHeader query {};
  uint16_t id = random_.random() & 0xFFFF;

  // Generate a random query ID
  query.id = id;

  // Signify that this is a query
  query.flags.qr = 0;

  // This should usually be zero
  query.flags.opcode = 0;

  query.flags.aa = 0;
  query.flags.tc = 0;

  // Set Recursion flags (at least one bit set so that the flags are not all zero)
  query.flags.rd = 1;
  query.flags.ra = 0;

  // reserved flag is not set
  query.flags.z = 0;

  // Set the authenticated flags to zero
  query.flags.ad = 0;
  query.flags.cd = 0;

  query.questions = 1;
  query.answers = 0;
  query.authority_rrs = 0;
  query.additional_rrs = 0;

  Buffer::OwnedImpl buffer_;
  buffer_.writeBEInt<uint16_t>(query.id);

  uint16_t flags;
  ::memcpy(&flags, static_cast<void*>(&query.flags), sizeof(uint16_t));
  buffer_.writeBEInt<uint16_t>(flags);

  buffer_.writeBEInt<uint16_t>(query.questions);
  buffer_.writeBEInt<uint16_t>(query.answers);
  buffer_.writeBEInt<uint16_t>(query.authority_rrs);
  buffer_.writeBEInt<uint16_t>(query.additional_rrs);

  DnsQueryRecord query_rec(name, rec_type, rec_class);
  query_rec.serialize(buffer_);
  return buffer_.toString();
}

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
