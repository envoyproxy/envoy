#include <netinet/ip.h>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/common/exception.h"
#include "envoy/network/address.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/address_range_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Network {
namespace Address {

namespace {

// kIpRangeResolverName may be used as a envoy::api::v2::core::SocketAddress::resolver_name.
// In this case, SocketAddress::named_port must be specified in the form "<startport>-<endport>"
// with <endport> >= <startport>.  Whitespace in the named_port field is ignored.
const char* const kIpRangeResolverName = "google.ipRange";

// Choosing with replacement, the probability of failing to find an unbound port
// in a port range of size r with f already bound ports after trying n times is
// p(f,r,n) = (f/r)^n
// Arbitrarily choosing a target probability of less than 0.5 that we will fail
// to find a port in a range that is 80% full yields:
// 	0.5 > (0.8)^n
// 	n > log(0.5)/log(0.8) =~ 3.1
// So we try four times to find a port.
//
// Note that choosing with replacement is only a good strategy for large port ranges;
// if InstanceRanges with only a few ports in them are used, this file should
// either choose without replacement or search linearly.
const int kBindingRangeNumberOfTries = 4;

uint32_t portToTry(uint32_t start, uint32_t end, Runtime::RandomGenerator& random) {
  double unitary_scaled_random_value =
      (static_cast<double>(random.random()) / std::numeric_limits<uint64_t>::max());
  uint32_t port_to_try = start + static_cast<uint32_t>(
      (end + 1 - start) * unitary_scaled_random_value);
  // port_to_try has prob(0) of being end + 1, but prob(0) != never.
  if (port_to_try == end + 1) {
    port_to_try = end;
  }
  return port_to_try;
}

}  // namespace

/**
 * Resolver to create IpInstanceRange.
 */
class IpRangeResolver : public Resolver {
public:
  InstanceConstSharedPtr
  resolve(const envoy::api::v2::core::SocketAddress& socket_address) override {
    switch (socket_address.port_specifier_case()) {
    case envoy::api::v2::core::SocketAddress::kNamedPort:
      return std::make_shared<IpInstanceRange>(
          Network::Utility::parseInternetAddress(
              socket_address.address(), socket_address.port_value(),
              !socket_address.ipv4_compat()),
          socket_address.named_port(),
          random_);

    default:
      throw EnvoyException(fmt::format("IP range resolver can't handle port specifier type {}",
                                       socket_address.port_specifier_case()));
    }
  }

  std::string name() const override { return kIpRangeResolverName; }
 private:
  Runtime::RandomGeneratorImpl random_;
};

/**
 * Static registration for the IP resolver. @see RegisterFactory.
 */
static Registry::RegisterFactory<IpRangeResolver, Resolver> ip_registered_;

IpInstanceRange::IpInstanceRange(InstanceConstSharedPtr&& address_instance,
                                 const std::string& range_as_named_port,
                                 Runtime::RandomGenerator& random)
    : address_instance_(std::move(address_instance)), start_port_(0), end_port_(0),
      random_(random) {
  std::vector<absl::string_view> ports(StringUtil::splitToken(range_as_named_port, "-"));
  if (address_instance_->ip()->port() != 0u) {
    throw EnvoyException(fmt::format(
        "IpInstanceRange initialized with address_instance with non-zero port \"{}\".",
        address_instance_->ip()->port()));
  }
  if (ports.size() != 2u) {
    throw EnvoyException(
        fmt::format("Named port value \"{}\" does not have \"<start>-<end>\" format",
                    range_as_named_port));
  }
  if (!absl::SimpleAtoi(ports[0], &start_port_)) {
    throw EnvoyException(
        fmt::format("Start port of named port value \"{}\" does not convert to integer.",
                    range_as_named_port));
  }
  if (!absl::SimpleAtoi(ports[1], &end_port_)) {
    throw EnvoyException(
        fmt::format("Start port of named port value \"{}\" does not convert to integer.",
                    range_as_named_port));
  }
  if (start_port_ > end_port_) {
    throw EnvoyException(
        fmt::format("Start port \"{}\" > end port \"{}\"", start_port_, end_port_));
  }
  friendly_name_ = address_instance_->ip()->addressAsString() + ":" +
                   std::to_string(start_port_) + "-" + std::to_string(end_port_);
}

int IpInstanceRange::bind(int fd) const {
  int tries = kBindingRangeNumberOfTries;
  while (tries--) {
    int ret = 0;
    switch (ip()->version()) {
    case IpVersion::v4:
      {
        sockaddr_in socket_address;
	socket_address.sin_addr.s_addr = address_instance_->ip()->ipv4()->address();
        socket_address.sin_family = AF_INET;
        socket_address.sin_port = htons(portToTry(start_port_, end_port_, random_));

        ret = ::bind(
            fd, reinterpret_cast<const sockaddr*>(&socket_address), sizeof(socket_address));
      }
      break;
    case IpVersion::v6:
      {
        sockaddr_in6 socket_address;
	absl::uint128 result(address_instance_->ip()->ipv6()->address());
	memcpy(static_cast<void*>(&socket_address.sin6_addr.s6_addr),
	       static_cast<void*>(&result), sizeof(absl::uint128));
        socket_address.sin6_family = AF_INET6;
        socket_address.sin6_port = htons(portToTry(start_port_, end_port_, random_));
        ret = ::bind(
            fd, reinterpret_cast<const sockaddr*>(&socket_address), sizeof(socket_address));
      }
      break;
    }
    if (ret == -1)
      ret = errno;
    if (ret != EADDRINUSE)  // Includes success
      return ret;
  }
  return EADDRINUSE;
}

int IpInstanceRange::connect(int /* fd */) const {
  throw EnvoyException("IpInstanceRange::connect not implemented.");
}

} // namespace Address
} // namespace Network
} // namespace Envoy
