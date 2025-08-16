#include "source/common/network/ip_address_parsing.h"

#include <cstring>

#include "source/common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Network {
namespace IpAddressParsing {

StatusOr<sockaddr_in> parseIPv4(const std::string& ip_address, uint16_t port) {
  // Prefer getaddrinfo() with ``AI_NUMERICHOST|AI_NUMERICSERV`` for consistency with IPv6 parsing
  // and to keep a single parsing method. This also ensures no DNS lookups occur.
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  struct addrinfo* res = nullptr;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  static Api::OsSysCallsImpl os_sys_calls;
  const Api::SysCallIntResult rc =
      os_sys_calls.getaddrinfo(ip_address.c_str(), /*service=*/nullptr, &hints, &res);
  if (rc.return_value_ != 0) {
    return absl::FailedPreconditionError(absl::StrCat("getaddrinfo error: ", rc.return_value_));
  }
  sockaddr_in sa4 = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
  os_sys_calls.freeaddrinfo(res);
  sa4.sin_port = htons(port);
  // Enforce strict dotted-quad by round-tripping via inet_ntop and requiring equality.
  char buf[INET_ADDRSTRLEN];
  const char* printed = inet_ntop(AF_INET, &sa4.sin_addr, buf, INET_ADDRSTRLEN);
  if (printed == nullptr || ip_address != printed) {
    return absl::FailedPreconditionError("failed parsing ipv4");
  }
  return sa4;
}

StatusOr<sockaddr_in6> parseIPv6(const std::string& ip_address, uint16_t port) {
  // Parse IPv6 with optional scope using getaddrinfo().
  // While inet_pton() would be faster and simpler, it does not support IPv6
  // addresses that specify a scope, e.g. `::%eth0` to listen on only one interface.
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  struct addrinfo* res = nullptr;
  // Suppresses any potentially lengthy network host address lookups and inhibit the
  // invocation of a name resolution service.
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  hints.ai_family = AF_INET6;
  // Given that we do not specify a service but we use getaddrinfo() to only parse the node
  // address, specifying the socket type allows to hint the getaddrinfo() to return only an
  // element with the below socket type. The behavior though remains platform dependent and
  // anyway we consume only the first element if the call succeeds.
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  // We want to use the interface of OsSysCalls for this for the platform-independence, but
  // we do not want to use the common OsSysCallsSingleton.
  //
  // The problem with using OsSysCallsSingleton is that we likely want to override getaddrinfo()
  // for DNS lookups in tests, but typically that override would resolve a name to e.g. the
  // address from resolveUrl("tcp://[::1]:80"). But resolveUrl calls ``parseIPv6``, which calls
  // getaddrinfo(), so if we use the mock here then mocking DNS causes infinite recursion.
  //
  // We do not ever need to mock this getaddrinfo() call, because it is only used to parse
  // numeric IP addresses, per ``ai_flags``, so it should be deterministic resolution. There
  // is no need to mock it to test failure cases.
  static Api::OsSysCallsImpl os_sys_calls;
  const Api::SysCallIntResult rc =
      os_sys_calls.getaddrinfo(ip_address.c_str(), /*service=*/nullptr, &hints, &res);
  if (rc.return_value_ != 0) {
    return absl::FailedPreconditionError(absl::StrCat("getaddrinfo error: ", rc.return_value_));
  }
  sockaddr_in6 sa6 = *reinterpret_cast<sockaddr_in6*>(res->ai_addr);
  os_sys_calls.freeaddrinfo(res);
  sa6.sin6_port = htons(port);
  return sa6;
}

} // namespace IpAddressParsing
} // namespace Network
} // namespace Envoy
