#include "common/config/address_json.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

void AddressJson::translateUnresolvedAddress(
    const std::string& json_address, bool url,
    envoy::api::v2::UnresolvedAddress& unresolved_address) {
  // TODO(htuch): No parsing required yet for either pipes or non-URL unresolved addresses, add as
  // needed.
  ASSERT(url);
  UNREFERENCED_PARAMETER(url);
  auto* named_address = unresolved_address.mutable_named_address();
  named_address->set_address(Network::Utility::hostFromTcpUrl(json_address));
  named_address->mutable_port()->set_value(Network::Utility::portFromTcpUrl(json_address));
}

void AddressJson::translateResolvedAddress(const std::string& json_address, bool url,
                                           envoy::api::v2::ResolvedAddress& resolved_address) {
  Network::Address::InstanceConstSharedPtr address =
      url ? Network::Utility::resolveUrl(json_address)
          : Network::Utility::parseInternetAddressAndPort(json_address);
  if (address->type() == Network::Address::Type::Ip) {
    auto* socket_address = resolved_address.mutable_socket_address();
    switch (address->ip()->version()) {
    case Network::Address::IpVersion::v4:
      socket_address->set_ip_address(address->ip()->addressAsString());
      break;
    case Network::Address::IpVersion::v6:
      socket_address->set_ip_address("[" + address->ip()->addressAsString() + "]");
      break;
    }
    socket_address->mutable_port()->set_value(address->ip()->port());
  } else {
    ASSERT(address->type() == Network::Address::Type::Pipe);
    resolved_address.mutable_pipe()->set_path(address->asString());
  }
}

} // namespace Config
} // namespace Envoy
