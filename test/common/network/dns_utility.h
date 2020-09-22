#include <list>
#include <string>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/network/dns.h"

namespace Envoy {
namespace Network {

static std::list<Address::InstanceConstSharedPtr>
getAddressList(const std::list<DnsResponse>& response) {
  std::list<Address::InstanceConstSharedPtr> address;

  for_each(response.begin(), response.end(),
           [&](DnsResponse resp) { address.emplace_back(resp.address_); });
  return address;
}

static std::list<std::string> getAddressAsStringList(const std::list<DnsResponse>& response) {
  std::list<std::string> address;

  for_each(response.begin(), response.end(),
           [&](DnsResponse resp) { address.emplace_back(resp.address_->ip()->addressAsString()); });
  return address;
}

} // namespace Network
} // namespace Envoy
