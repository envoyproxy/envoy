#pragma once

#include "envoy/json/json_object.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"

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

  bool contains(const Address::Instance& address) const;
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
   * Resolve a URL.
   * @param url supplies the url to resolve.
   * @return a resolved address.
   */
  static Address::InstancePtr resolveUrl(const std::string& url);

  /**
   * Parses the host from a TCP URL
   * @param the URL to parse host from
   * @return std::string the parsed host
   */
  static std::string hostFromTcpUrl(const std::string& url);

  /**
   * Parses the port from a TCP URL
   * @param the URL to parse port from
   * @return uint32_t the parsed port
   */
  static uint32_t portFromTcpUrl(const std::string& url);

  /**
   * @return the local IP address of the server
   */
  static Address::InstancePtr getLocalAddress();

  /**
   * Determine whether this is an internal (RFC1918) address.
   * @return bool the address is an RFC1918 address.
   */
  static bool isInternalAddress(const char* address);

  /**
   * Check if address is loopback address.
   * @return true if so, otherwise false
   */
  static bool isLoopbackAddress(const Address::Instance& address);

  /**
   * Retrieve the original destination address from an accepted fd.
   * The address (IP and port) may be not local and the port may differ from
   * the listener port if the packets were redirected using iptables
   * @param fd is the descriptor returned by accept()
   * @return the original destination or nullptr not available.
   */
  static Address::InstancePtr getOriginalDst(int fd);

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
   * @param address supplies the IP address to compare.
   * @param list the list of port ranges in which the port may appear
   * @return whether the port appears in at least one of the ranges in the list
   */
  static bool portInRangeList(const Address::Instance& address, const std::list<PortRange>& list);
};

} // Network
