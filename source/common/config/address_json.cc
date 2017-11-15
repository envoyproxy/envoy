#include "common/config/address_json.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

void AddressJson::translateAddress(const std::string& json_address, bool url, bool resolved,
                                   envoy::api::v2::Address& address) {
  if (resolved) {
    Network::Address::InstanceConstSharedPtr instance =
        url ? Network::Utility::resolveUrl(json_address)
            : Network::Utility::parseInternetAddressAndPort(json_address);
    if (instance->type() == Network::Address::Type::Ip) {
      address.mutable_socket_address()->set_address(instance->ip()->addressAsString());
      address.mutable_socket_address()->set_port_value(instance->ip()->port());
    } else {
      ASSERT(instance->type() == Network::Address::Type::Pipe);
      address.mutable_pipe()->set_path(instance->asString());
    }
    return;
  }

  // We don't have v1 JSON with unresolved addresses in non-URL form.
  ASSERT(url);
  // Non-TCP scheme (e.g. Unix scheme) is not supported with unresolved address.
  if (!Network::Utility::urlIsTcpScheme(json_address)) {
    throw EnvoyException(fmt::format("unresolved URL must be TCP scheme, got: {}", json_address));
  }
  address.mutable_socket_address()->set_address(Network::Utility::hostFromTcpUrl(json_address));
  address.mutable_socket_address()->set_port_value(Network::Utility::portFromTcpUrl(json_address));
}

} // namespace Config
} // namespace Envoy
