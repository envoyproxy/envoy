#pragma once

#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

#include "common/json/json_loader.h"
#include "common/network/addr_info.h"

#include <sys/un.h>

#include <linux/netfilter_ipv4.h>

namespace Network {

/**
 * Utility class for keeping a list of IPV4 addresses and masks, and then determining whether an
 * IP address is in the address/mask list.
 */
class IpList {
public:
  IpList(const std::vector<std::string>& subnets);
  IpList(const Json::Object& config, const std::string& member_name);
  IpList(){};

  bool contains(const std::string& address) const;
  bool empty() const { return ipv4_list_.empty(); }

private:
  struct Ipv4Entry {
    uint32_t ipv4_address_;
    uint32_t ipv4_mask_;
  };

  std::vector<Ipv4Entry> ipv4_list_;
};

/**
 * Utility class to represent TCP/UDP port range
 */
class PortRange {
public:
  PortRange(uint32_t min, uint32_t max) : min_(min), max_(max) {}

  bool contains(uint32_t port) const { return (port >= min_ && port <= max_); }

private:
  const uint32_t min_;
  const uint32_t max_;
};

typedef std::list<PortRange> PortRangeList;

/**
 * Common network utility routines.
 */
class Utility {
public:
  static const std::string TCP_SCHEME;
  static const std::string UNIX_SCHEME;

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
   * Extract port information from a sockaddr_in.
   * @param addr the address from which to extract the port number
   * @return the port number
   */
  static uint16_t getAddressPort(sockaddr_in* addr);

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

  /**
   * Retrieve the original destination address from an accepted fd.
   * The address (IP and port) may be not local and the port may differ from
   * the listener port if the packets were redirected using iptables
   * @param fd is the descriptor returned by accept()
   * @param orig_addr is the data structure that contains the original address
   * @return true if the operation succeeded, false otherwise
   */
  static bool getOriginalDst(int fd, sockaddr_storage* orig_addr);

  /**
   * Parses a string containing a comma-separated list of port numbers and/or
   * port ranges and appends the values to a caller-provided list of PortRange structures.
   * For example, the string "1-1024,2048-4096,12345" causes 3 PortRange structures
   * to be appended to the supplied list.
   * @param str is the string containing the port numbers and ranges
   * @param list is the list to append the new data structures to
   */
  static void parsePortRangeList(const std::string& string, std::list<PortRange>& list);

  /**
   * Checks whether a given port number appears in at least one of the port ranges in a list
   * @param port is the port number to search
   * @param list the list of port ranges in which the port may appear
   * @return whether the port appears in at least one of the ranges in the list
   */
  static bool portInRangeList(uint32_t port, const std::list<PortRange>& list);
};

} // Network
