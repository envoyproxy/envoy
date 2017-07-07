#include "server/config_validation/dns.h"

namespace Envoy {
namespace Network {

ActiveDnsQuery* ValidationDnsResolver::resolve(const std::string&, DnsLookupFamily,
                                               ResolveCb callback) {
  callback({});
  return nullptr;
}

} // namespace Network
} // namespace Envoy
