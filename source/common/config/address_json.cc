#include "common/config/address_json.h"

#include "common/common/assert.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Config {

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
