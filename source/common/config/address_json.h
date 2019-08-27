#pragma once

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/json/json_object.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class AddressJson {
public:
  /**
   * Translate a v1 JSON array of IP ranges to v2
   * Protobuf::RepeatedPtrField<envoy::api::v2::core::CidrRange>.
   * @param json_ip_list List of IP ranges, such as "1.1.1.1/24"
   * @param range_list destination
   */
  static void
  translateCidrRangeList(const std::vector<std::string>& json_ip_list,
                         Protobuf::RepeatedPtrField<envoy::api::v2::core::CidrRange>& range_list);
};

} // namespace Config
} // namespace Envoy
