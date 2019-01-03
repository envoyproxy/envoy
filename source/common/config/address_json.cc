#include "common/config/address_json.h"

#include "common/common/assert.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

void AddressJson::translateAddress(const std::string& json_address, bool url, bool resolved,
                                   envoy::api::v2::core::Address& address) {
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

void AddressJson::translateCidrRangeList(
    const std::vector<std::string>& json_ip_list,
    Protobuf::RepeatedPtrField<envoy::api::v2::core::CidrRange>& range_list) {
  for (const std::string& source_ip : json_ip_list) {
    Network::Address::CidrRange cidr(Network::Address::CidrRange::create(source_ip));
    if (!cidr.isValid()) {
      throw EnvoyException(fmt::format("Invalid cidr entry: {}", source_ip));
    }
    envoy::api::v2::core::CidrRange* v2_cidr = range_list.Add();
    v2_cidr->set_address_prefix(cidr.ip()->addressAsString());
    v2_cidr->mutable_prefix_len()->set_value(cidr.length());
  }
}

} // namespace Config
} // namespace Envoy
