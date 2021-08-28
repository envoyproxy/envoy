#pragma once

#include <string>
#include <vector>

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

private:
  static std::vector<std::string> enumerateInterfaces(unsigned short family);
};

} // namespace Network
} // namespace Envoy
