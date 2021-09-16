#pragma once

#include <string>
#include <vector>

#include "envoy/network/socket.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Network {

/**
 * Network utility routines related to mobile clients.
 */
class MobileUtility {
public:
  /**
   * @returns a list of local network interfaces supporting IPv4.
   */
  static std::vector<std::string> enumerateV4Interfaces();

  /**
   * @returns a list of local network interfaces supporting IPv6.
   */
  static std::vector<std::string> enumerateV6Interfaces();

  /**
   * @returns the current OS default/preferred network class.
   */
  static envoy_network_t getPreferredNetwork();

  /**
   * Sets the current OS default/preferred network class.
   * @param network, the network preference.
   */
  static void setPreferredNetwork(envoy_network_t network);

  /**
   * @returns the current socket options that should be used for connections.
   */
  static Socket::OptionsSharedPtr getUpstreamSocketOptions(envoy_network_t network);

private:
  static std::vector<std::string> enumerateInterfaces(unsigned short family);
  static std::atomic<envoy_network_t> preferred_network_;
};

} // namespace Network
} // namespace Envoy
