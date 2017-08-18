#pragma once

#include "envoy/json/json_object.h"

#include "api/address.pb.h"

namespace Envoy {
namespace Config {

class AddressJson {
public:
  /**
   * Translate a v1 JSON address to v2 envoy::api::v2::Address.
   * @param json_address source address.
   * @param url is json_address a URL? E.g. tcp://<ip>:<port>. If not, it is
   *            treated as <ip>:<port>.
   * @param resolved is json_address a concrete IP/pipe or unresolved hostname?
   * @param address destination envoy::api::v2::Address.
   */
  static void translateAddress(const std::string& json_address, bool url, bool resolved,
                               envoy::api::v2::Address& address);
};

} // namespace Config
} // namespace Envoy
