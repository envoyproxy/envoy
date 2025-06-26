#pragma once

namespace Envoy {

/**
 * Networks classified by the physical link.
 * In real world the network type can be compounded, e.g. wifi with vpn.
 * Enums values in this class will be AND'ed to form the compound type.
 */
enum class NetworkType : int {
  // Includes VPN or cases where network characteristics are unknown.
  Generic = 1, // 001
  // Includes WiFi and other local area wireless networks.
  WLAN = 2, // 010
  // Includes all mobile phone networks.
  WWAN = 4, // 100
  // Includes 2G networks.
  WWAN_2G = 8, // 1000
  // Includes 3G networks.
  WWAN_3G = 16, // 10000
  // Includes 4G networks.
  WWAN_4G = 32, // 100000
  // Includes 5G networks.
  WWAN_5G = 64, // 1000000
};

} // namespace Envoy
