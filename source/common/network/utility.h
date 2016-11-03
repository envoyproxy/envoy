#pragma once

#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

#include "common/json/json_loader.h"
#include "common/network/addr_info.h"

#include <sys/un.h>

namespace Network {

/**
 * Utility class for keeping a list of IPV4 addresses and masks, and then determining whether an
 * IP address is in the address/mask list.
 */
class IpWhiteList {
public:
  IpWhiteList(const Json::Object& config);

  bool contains(const std::string& remote_address) const;

private:
  struct Ipv4Entry {
    uint32_t ipv4_address_;
    uint32_t ipv4_mask_;
  };

  std::vector<Ipv4Entry> ipv4_white_list_;
};

/**
 * Common network utility routines.
 */
class Utility {
public:
  static const std::string TCP_SCHEME;
  static const std::string UNIX_SCHEME;

  /**
   * Update buffering stats for a connection. Meant to be paired with
   * ConnectionCallbacks::onBufferChange().
   */
  static void updateBufferStats(ConnectionBufferType type, int64_t delta, Stats::Counter& rx_total,
                                Stats::Gauge& rx_buffered, Stats::Counter& tx_total,
                                Stats::Gauge& tx_buffered);

  /**
   * Resolve a TCP address.
   * @param host supplies the host name.
   * @param port supplies the port.
   * @return EventAddrInfoPtr the resolved address.
   */
  static AddrInfoPtr resolveTCP(const std::string& host, uint32_t port);

  /**
   * Resolve a unix domain socket.
   * @param path supplies the path to resolve.
   * @return EventAddrInfoPtr the resolved address.
   */
  static sockaddr_un resolveUnixDomainSocket(const std::string& path);

  /**
   * Resolve an address.
   * @param url supplies the url to resolve.
   * @return EventAddrInfoPtr the resolved address.
   */
  static void resolve(const std::string& url);

  /**
   * Parses the host from a URL
   * @param the URL to parse host from
   * @return std::string the parsed host
   */
  static std::string hostFromUrl(const std::string& url);

  /**
   * Parses the port from a URL
   * @param the URL to parse port from
   * @return uint32_t the parsed port
   */
  static uint32_t portFromUrl(const std::string& url);

  /**
   * Parses a path from a URL
   * @param the URL to parse port from
   * @return std::string the parsed path
   */
  static std::string pathFromUrl(const std::string& url);

  /**
   * Converts an address and port into a TCP URL
   * @param address the address to include
   * @param port the port to include
   * @return URL a URL of the form tcp://address:port
   */
  static std::string urlForTcp(const std::string& address, uint32_t port);

  /**
   * @return the local IP address of the server
   */
  static std::string getLocalAddress();

  /**
   * Converts a sockaddr_in to a human readable string.
   * @param addr the address to convert to a string
   * @return the string IP address representation for 'addr'
   */
  static std::string getAddressName(sockaddr_in* addr);

  /**
   * Determine whether this is an internal (RFC1918) address.
   * @return bool the address is an RFC1918 address.
   */
  static bool isInternalAddress(const char* address);

  /**
   * Check if address is loopback address.
   * @return true if so, otherwise false
   */
  static bool isLoopbackAddress(const char* address);
};

} // Network
