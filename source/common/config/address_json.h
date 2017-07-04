#pragma once

#include "envoy/json/json_object.h"

#include "api/address.pb.h"

namespace Envoy {
namespace Config {

class AddressJson {
public:
  /**
   * Translate a v1 JSON address to v2 envoy::api::v2::UnresolvedAddress.
   * @param json_address source address.
   * @param url is json_address a URL? E.g. tcp://<ip>:<port>. If not, it is
   *            treated as <ip>:<port>.
   * @param unresolved_address destination envoy::api::v2::UnresolvedAddress.
   */
  static void translateUnresolvedAddress(const std::string& json_address, bool url,
                                         envoy::api::v2::UnresolvedAddress& unresolved_address);

  /**
   * Translate a v1 JSON address to v2 envoy::api::v2::ResolvedAddress.
   * @param json_address source address.
   * @param url is json_address a URL? E.g. tcp://<ip>:<port>. If not, it is
   *            treated as <ip>:<port>.
   * @param resolved_address destination envoy::api::v2::ResolvedAddress.
   */
  static void translateResolvedAddress(const std::string& json_address, bool url,
                                       envoy::api::v2::ResolvedAddress& resolved_address);
};

} // namespace Config
} // namespace Envoy
