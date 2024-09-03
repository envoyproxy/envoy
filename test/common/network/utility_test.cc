#ifndef WIN32
#include <net/if.h>

#else
#include <winsock2.h>
#include <iphlpapi.h>
#endif

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"

#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::DoAll;
using testing::Eq;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {

struct Interface {
  std::string name;
  uint32_t if_index;
};

// Helper function that returns any usable interface present while running the test.
StatusOr<Interface> getLocalNetworkInterface() {
  if (!Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    return absl::FailedPreconditionError("getifaddrs not supported");
  }
  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
  ASSERT(rc.return_value_ == 0);
  if (!interface_addresses.empty()) {
    for (const auto& ifc : interface_addresses) {
      Interface interface;
      interface.name = ifc.interface_name_;
      interface.if_index = if_nametoindex(ifc.interface_name_.c_str());
      return interface;
    }
  }
  return absl::NotFoundError("no interface available");
}

TEST(NetworkUtility, resolveUrl) {
  EXPECT_FALSE(Utility::resolveUrl("foo").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("abc://foo").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://1.2.3.4:1234/").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://127.0.0.1:8001/").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://127.0.0.1:0/foo").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://127.0.0.1:").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://192.168.3.3").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://192.168.3.3.3:0").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://192.168.3:0").status().ok());

  EXPECT_FALSE(Utility::resolveUrl("udp://1.2.3.4:1234/").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://127.0.0.1:8001/").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://127.0.0.1:0/foo").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://127.0.0.1:").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://192.168.3.3").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://192.168.3.3.3:0").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://192.168.3:0").status().ok());

  EXPECT_FALSE(Utility::resolveUrl("tcp://[::1]").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://[:::1]:1").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("tcp://foo:0").status().ok());

  EXPECT_FALSE(Utility::resolveUrl("udp://[::1]").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://[:::1]:1").status().ok());
  EXPECT_FALSE(Utility::resolveUrl("udp://foo:0").status().ok());

  EXPECT_EQ("", (*Utility::resolveUrl("unix://"))->asString());
  EXPECT_EQ("foo", (*Utility::resolveUrl("unix://foo"))->asString());
  EXPECT_EQ("tmp", (*Utility::resolveUrl("unix://tmp"))->asString());
  EXPECT_EQ("tmp/server", (*Utility::resolveUrl("unix://tmp/server"))->asString());

  EXPECT_EQ("1.2.3.4:1234", (*Utility::resolveUrl("tcp://1.2.3.4:1234"))->asString());
  EXPECT_EQ("0.0.0.0:0", (*Utility::resolveUrl("tcp://0.0.0.0:0"))->asString());
  EXPECT_EQ("127.0.0.1:0", (*Utility::resolveUrl("tcp://127.0.0.1:0"))->asString());

  EXPECT_EQ("[::1]:1", (*Utility::resolveUrl("tcp://[::1]:1"))->asString());
  EXPECT_EQ("[::]:0", (*Utility::resolveUrl("tcp://[::]:0"))->asString());
  EXPECT_EQ("[1::2:3]:4", (*Utility::resolveUrl("tcp://[1::2:3]:4"))->asString());
  EXPECT_EQ("[a::1]:0", (*Utility::resolveUrl("tcp://[a::1]:0"))->asString());
  EXPECT_EQ("[a:b:c:d::]:0", (*Utility::resolveUrl("tcp://[a:b:c:d::]:0"))->asString());

  EXPECT_EQ("1.2.3.4:1234", (*Utility::resolveUrl("udp://1.2.3.4:1234"))->asString());
  EXPECT_EQ("0.0.0.0:0", (*Utility::resolveUrl("udp://0.0.0.0:0"))->asString());
  EXPECT_EQ("127.0.0.1:0", (*Utility::resolveUrl("udp://127.0.0.1:0"))->asString());

  EXPECT_EQ("[::1]:1", (*Utility::resolveUrl("udp://[::1]:1"))->asString());
  EXPECT_EQ("[::]:0", (*Utility::resolveUrl("udp://[::]:0"))->asString());
  EXPECT_EQ("[1::2:3]:4", (*Utility::resolveUrl("udp://[1::2:3]:4"))->asString());
  EXPECT_EQ("[a::1]:0", (*Utility::resolveUrl("udp://[a::1]:0"))->asString());
  EXPECT_EQ("[a:b:c:d::]:0", (*Utility::resolveUrl("udp://[a:b:c:d::]:0"))->asString());
}

TEST(NetworkUtility, urlFromDatagramAddress) {
  // UDP and unix URLs should be reversible with resolveUrl and urlFromDatagramAddress.
  std::vector<std::string> urls{
      "udp://[::1]:1", "udp://[a:b:c:d::]:0", "udp://1.2.3.4:1234", "unix://foo", "unix://",
  };
  for (const std::string& url : urls) {
    EXPECT_EQ(url, Utility::urlFromDatagramAddress(**Utility::resolveUrl(url)));
  }
}

TEST(NetworkUtility, socketTypeFromUrl) {
  EXPECT_FALSE(Utility::socketTypeFromUrl("foo").ok());
  EXPECT_FALSE(Utility::socketTypeFromUrl("abc://foo").ok());

  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://foo"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://tmp/server"));

  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://1.2.3.4:1234"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://0.0.0.0:0"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://[::1]:1"));

  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://1.2.3.4:1234"));
  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://0.0.0.0:0"));
  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://[::1]:1"));
}

TEST(NetworkUtility, ParseInternetAddress) {
  EXPECT_EQ(Utility::parseInternetAddressNoThrow(""), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("1.2.3"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("1.2.3.4.5"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("1.2.3.256"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("foo"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("0:0:0:0"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("fffff::"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("/foo"), nullptr);

  // TODO(#24326): make windows getaddrinfo more strict. See below example.
#ifndef WIN32
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("[::]"), nullptr);
  EXPECT_EQ(Utility::parseInternetAddressNoThrow("[::1]:1"), nullptr);
#endif

  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow(""));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3.4.5"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3.256"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("foo"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("0:0:0:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("fffff::"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("/foo"));

#ifndef WIN32
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("[::]"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("[::1]:1"));
#else
  // TODO(#24326): make windows getaddrinfo more strict.
  EXPECT_EQ("[::]:0", Utility::parseInternetAddressNoThrow("[::]")->asString());
  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressNoThrow("[::1]:1")->asString());
#endif
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("fe80::1%"));

  EXPECT_EQ("1.2.3.4:0", Utility::parseInternetAddressNoThrow("1.2.3.4")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressNoThrow("0.0.0.0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressNoThrow("127.0.0.1")->asString());

  EXPECT_EQ("1.2.3.4:0", Utility::parseInternetAddressNoThrow("1.2.3.4")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressNoThrow("0.0.0.0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressNoThrow("127.0.0.1")->asString());

  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressNoThrow("::1")->asString());
  EXPECT_EQ("[::]:0", Utility::parseInternetAddressNoThrow("::")->asString());
  EXPECT_EQ("[1::2:3]:0", Utility::parseInternetAddressNoThrow("1::2:3")->asString());
  EXPECT_EQ("[a::1]:0", Utility::parseInternetAddressNoThrow("a::1")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::parseInternetAddressNoThrow("a:b:c:d::")->asString());

  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressNoThrow("::1")->asString());
  EXPECT_EQ("[::]:0", Utility::parseInternetAddressNoThrow("::")->asString());
  EXPECT_EQ("[1::2:3]:0", Utility::parseInternetAddressNoThrow("1::2:3")->asString());
  EXPECT_EQ("[a::1]:0", Utility::parseInternetAddressNoThrow("a::1")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::parseInternetAddressNoThrow("a:b:c:d::")->asString());

  StatusOr<Interface> ifc = getLocalNetworkInterface();
  if (ifc.ok()) {
    EXPECT_EQ(
        absl::StrCat("[fe80::1%", ifc->if_index, "]:0"),
        Utility::parseInternetAddressNoThrow(absl::StrCat("fe80::1%", ifc->name))->asString());
    EXPECT_EQ(
        absl::StrCat("[fe80::1%", ifc->if_index, "]:0"),
        Utility::parseInternetAddressNoThrow(absl::StrCat("fe80::1%", ifc->if_index))->asString());
    EXPECT_NE(*Utility::parseInternetAddressNoThrow("fe80::1"),
              *Utility::parseInternetAddressNoThrow(absl::StrCat("fe80::1%", ifc->if_index)));
  }
}

TEST(NetworkUtility, ParseInternetAddressAndPort) {
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4::1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:-1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(":1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(" :1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3:1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:65536"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:8008/"));

  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressAndPortNoThrow("0.0.0.0:0")->asString());
  EXPECT_EQ("255.255.255.255:65535",
            Utility::parseInternetAddressAndPortNoThrow("255.255.255.255:65535")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:0")->asString());

  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(""));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("::1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("::"));

  // TODO(#24326): make windows getaddrinfo more strict. See above example.
#ifndef WIN32
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[[::]]:1"));
#endif
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:1]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("]:[::1]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1.2.3.4:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1.2.3.4]:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:-1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:bogus"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1::1]:65536"));

  EXPECT_EQ("[::]:0", Utility::parseInternetAddressAndPortNoThrow("[::]:0")->asString());
  EXPECT_EQ("[1::1]:65535",
            Utility::parseInternetAddressAndPortNoThrow("[1::1]:65535")->asString());
  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressAndPortNoThrow("[::1]:0")->asString());
}

class NetworkUtilityGetLocalAddress : public testing::TestWithParam<Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkUtilityGetLocalAddress,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

ACTION_P(SetArg2Int, val) { *(static_cast<int*>(arg2)) = val; }

ACTION_P(SetArg2Uint32, val) { *(static_cast<uint32_t*>(arg2)) = val; }

ACTION_P(SetArg1Sockaddr, val) {
  const sockaddr_in& sin = reinterpret_cast<const sockaddr_in&>(val);
  (reinterpret_cast<sockaddr_in*>(arg1))->sin_addr = sin.sin_addr;
  (reinterpret_cast<sockaddr_in*>(arg1))->sin_family = sin.sin_family;
  (reinterpret_cast<sockaddr_in*>(arg1))->sin_port = sin.sin_port;
}

ACTION_P(SetArg1Sockaddr6, val) {
  const sockaddr_in6& sin6 = reinterpret_cast<const sockaddr_in6&>(val);
  (reinterpret_cast<sockaddr_in6*>(arg1))->sin6_addr = sin6.sin6_addr;
  (reinterpret_cast<sockaddr_in6*>(arg1))->sin6_family = sin6.sin6_family;
  (reinterpret_cast<sockaddr_in6*>(arg1))->sin6_port = sin6.sin6_port;
}

ACTION_P(SetArg2Sockaddr, val) {
  const sockaddr_in& sin = reinterpret_cast<const sockaddr_in&>(val);
  (static_cast<sockaddr_in*>(arg2))->sin_addr = sin.sin_addr;
  (static_cast<sockaddr_in*>(arg2))->sin_family = sin.sin_family;
  (static_cast<sockaddr_in*>(arg2))->sin_port = sin.sin_port;
}

ACTION_P(SetArg2Sockaddr6, val) {
  const sockaddr_in6& sin6 = reinterpret_cast<const sockaddr_in6&>(val);
  (static_cast<sockaddr_in6*>(arg2))->sin6_addr = sin6.sin6_addr;
  (static_cast<sockaddr_in6*>(arg2))->sin6_family = sin6.sin6_family;
  (static_cast<sockaddr_in6*>(arg2))->sin6_port = sin6.sin6_port;
}

TEST_P(NetworkUtilityGetLocalAddress, GetLocalAddress) {
  auto ip_version = GetParam();
  auto local_address = Utility::getLocalAddress(ip_version);
  EXPECT_NE(nullptr, local_address);
  EXPECT_EQ(ip_version, local_address->ip()->version());
  if (ip_version == Address::IpVersion::v6) {
    EXPECT_EQ(0u, local_address->ip()->ipv6()->scopeId());
  }
}

TEST_P(NetworkUtilityGetLocalAddress, GetLocalAddressGetifaddrsFailure) {
  Api::SysCallIntResult rc;
  rc.return_value_ = -1;
  rc.errno_ = 42;
  testing::StrictMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, getifaddrs(_)).WillRepeatedly(Return(rc));
  Address::IpVersion ip_version = GetParam();
  Address::InstanceConstSharedPtr expected_address;
  if (ip_version == Address::IpVersion::v6) {
    expected_address = std::make_shared<Address::Ipv6Instance>("::1");
  } else {
    expected_address = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
  }
  Address::InstanceConstSharedPtr local_address = Utility::getLocalAddress(ip_version);
  EXPECT_EQ(ip_version, local_address->ip()->version());
  EXPECT_NE(nullptr, local_address);
  EXPECT_EQ(*expected_address, *local_address);
}

TEST(NetworkUtility, GetOriginalDst) {
  testing::NiceMock<Network::MockConnectionSocket> socket;
#ifdef SOL_IP
  EXPECT_CALL(socket, ipVersion()).WillOnce(testing::Return(absl::nullopt));
#endif
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));

#ifdef SOL_IP
  EXPECT_CALL(socket, addressType()).WillOnce(testing::Return(Address::Type::Pipe));
#endif
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));

#ifdef SOL_IP
  sockaddr_storage storage;
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(socket, addressType()).WillRepeatedly(Return(Address::Type::Ip));

  auto& sin = reinterpret_cast<sockaddr_in&>(storage);
  sin.sin_family = AF_INET;
  sin.sin_port = htons(9527);
  sin.sin_addr.s_addr = inet_addr("12.34.56.78");
  EXPECT_CALL(socket, ipVersion()).WillRepeatedly(Return(Address::IpVersion::v4));
  // Socket gets original dst from SO_ORIGINAL_DST while connection tracking enabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IP), Eq(SO_ORIGINAL_DST), _, _))
      .WillOnce(DoAll(SetArg2Sockaddr(storage), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ("12.34.56.78:9527", Utility::getOriginalDst(socket)->asString());
#ifndef WIN32
  // Transparent socket gets original dst from local address while connection tracking disabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IP), Eq(SO_ORIGINAL_DST), _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, supportsIpTransparent(Address::IpVersion::v4)).WillOnce(Return(true));
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IP), Eq(IP_TRANSPARENT), _, _))
      .WillOnce(DoAll(SetArg2Int(1), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillOnce(DoAll(SetArg1Sockaddr(storage), SetArg2Uint32(sizeof(sockaddr_in)),
                      Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ("12.34.56.78:9527", Utility::getOriginalDst(socket)->asString());
  // Non-transparent socket fails to get original dst while connection tracking disabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IP), Eq(SO_ORIGINAL_DST), _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, supportsIpTransparent(Address::IpVersion::v4)).WillOnce(Return(true));
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IP), Eq(IP_TRANSPARENT), _, _))
      .WillOnce(DoAll(SetArg2Int(0), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));
#endif // WIN32

  auto& sin6 = reinterpret_cast<sockaddr_in6&>(storage);
  sin6.sin6_family = AF_INET6;
  sin6.sin6_port = htons(9527);
  EXPECT_EQ(1, inet_pton(AF_INET6, "12::34", &sin6.sin6_addr));
  EXPECT_CALL(socket, ipVersion()).WillRepeatedly(Return(Address::IpVersion::v6));
  // Socket gets original dst from SO_ORIGINAL_DST while connection tracking enabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IPV6), Eq(IP6T_SO_ORIGINAL_DST), _, _))
      .WillOnce(DoAll(SetArg2Sockaddr6(storage), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ("[12::34]:9527", Utility::getOriginalDst(socket)->asString());
#ifndef WIN32
  // Transparent socket gets original dst from local address while connection tracking disabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IPV6), Eq(IP6T_SO_ORIGINAL_DST), _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, supportsIpTransparent(Address::IpVersion::v6)).WillOnce(Return(true));
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IPV6), Eq(IPV6_TRANSPARENT), _, _))
      .WillOnce(DoAll(SetArg2Int(1), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_CALL(os_sys_calls, getsockname(_, _, _))
      .WillOnce(DoAll(SetArg1Sockaddr6(storage), SetArg2Uint32(sizeof(sockaddr_in6)),
                      Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ("[12::34]:9527", Utility::getOriginalDst(socket)->asString());
  // Non-transparent socket fails to get original dst while connection tracking disabled
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IPV6), Eq(IP6T_SO_ORIGINAL_DST), _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, 0}));
  EXPECT_CALL(os_sys_calls, supportsIpTransparent(Address::IpVersion::v6)).WillOnce(Return(true));
  EXPECT_CALL(socket, getSocketOption(Eq(SOL_IPV6), Eq(IPV6_TRANSPARENT), _, _))
      .WillOnce(DoAll(SetArg2Int(0), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));
#endif // WIN32

#endif // SOL_IP
}

TEST(NetworkUtility, LocalConnection) {
  testing::NiceMock<Network::MockConnectionSocket> socket;

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  socket.connection_info_provider_->setRemoteAddress(
      *Network::Address::PipeInstance::create("/pipe/path"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      *Network::Address::PipeInstance::create("/pipe/path"));
  socket.connection_info_provider_->setRemoteAddress(
      *Network::Address::PipeInstance::create("/pipe/path"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("8.8.8.8"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4", 1234));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4", 4321));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::3"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2", 4321));
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2", 1234));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("fd00::"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket.connectionInfoProvider()));
}

TEST(NetworkUtility, InternalAddress) {
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("127.0.0.1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("10.0.0.1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("192.168.0.0")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("172.16.0.0")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("172.30.2.1")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("192.167.0.0")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("172.32.0.0")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("11.0.0.1")));

  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fd00::")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("::1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fdff::")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fd01::")));
  EXPECT_TRUE(
      Utility::isInternalAddress(Address::Ipv6Instance("fd12:3456:7890:1234:5678:9012:3456:7890")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fd::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fc00::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fe00::")));

  EXPECT_FALSE(Utility::isInternalAddress(**Address::PipeInstance::create("/hello")));
}

TEST(NetworkUtility, LoopbackAddress) {
  {
    Address::Ipv4Instance address("127.0.0.1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv4Instance address("10.0.0.1");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    auto address = *Address::PipeInstance::create("/foo");
    EXPECT_FALSE(Utility::isLoopbackAddress(*address));
  }
  {
    Address::Ipv6Instance address("::1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv6Instance address("::");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  EXPECT_EQ("127.0.0.1:0", Utility::getCanonicalIpv4LoopbackAddress()->asString());
  EXPECT_EQ("[::1]:0", Utility::getIpv6LoopbackAddress()->asString());
}

TEST(NetworkUtility, AnyAddress) {
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv4AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v4);
    EXPECT_EQ(any->asString(), "0.0.0.0:0");
    EXPECT_EQ(any, Utility::getIpv4AnyAddress());
  }
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv6AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v6);
    EXPECT_EQ(any->asString(), "[::]:0");
    EXPECT_EQ(any, Utility::getIpv6AnyAddress());
  }
}

TEST(NetworkUtility, ParseProtobufAddress) {
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_address("127.0.0.1");
    proto_address.mutable_socket_address()->set_port_value(1234);
    EXPECT_EQ("127.0.0.1:1234",
              Utility::protobufAddressToAddressNoThrow(proto_address)->asString());
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_address("::1");
    proto_address.mutable_socket_address()->set_port_value(1234);
    EXPECT_EQ("[::1]:1234", Utility::protobufAddressToAddressNoThrow(proto_address)->asString());
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe()->set_path("/tmp/unix-socket");
    EXPECT_EQ("/tmp/unix-socket",
              Utility::protobufAddressToAddressNoThrow(proto_address)->asString());
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_envoy_internal_address()->set_server_listener_name("internal_listener");
    proto_address.mutable_envoy_internal_address()->set_endpoint_id("12345");
    EXPECT_EQ("envoy://internal_listener/12345",
              Utility::protobufAddressToAddressNoThrow(proto_address)->asString());
  }
#if defined(__linux__)
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe()->set_path("@/tmp/abstract-unix-socket");
    EXPECT_EQ("@/tmp/abstract-unix-socket",
              Utility::protobufAddressToAddressNoThrow(proto_address)->asString());
  }
#endif
}

TEST(NetworkUtility, AddressToProtobufAddress) {
  {
    envoy::config::core::v3::Address proto_address;
    Address::Ipv4Instance address("127.0.0.1");
    Utility::addressToProtobufAddress(address, proto_address);
    EXPECT_EQ(true, proto_address.has_socket_address());
    EXPECT_EQ("127.0.0.1", proto_address.socket_address().address());
    EXPECT_EQ(0, proto_address.socket_address().port_value());
  }
  {
    envoy::config::core::v3::Address proto_address;
    auto address = *Address::PipeInstance::create("/hello");
    Utility::addressToProtobufAddress(*address, proto_address);
    EXPECT_EQ(true, proto_address.has_pipe());
    EXPECT_EQ("/hello", proto_address.pipe().path());
  }
  {
    envoy::config::core::v3::Address proto_address;
    Address::EnvoyInternalInstance address("internal_address", "endpoint_id");
    Utility::addressToProtobufAddress(address, proto_address);
    EXPECT_TRUE(proto_address.has_envoy_internal_address());
    EXPECT_EQ("internal_address", proto_address.envoy_internal_address().server_listener_name());
    EXPECT_EQ("endpoint_id", proto_address.envoy_internal_address().endpoint_id());
  }
}

TEST(NetworkUtility, ProtobufAddressSocketType) {
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address();
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_protocol(
        envoy::config::core::v3::SocketAddress::TCP);
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_protocol(
        envoy::config::core::v3::SocketAddress::UDP);
    EXPECT_EQ(Socket::Type::Datagram, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe();
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
}

TEST(AbslUint128, TestByteOrder) {
#if defined(ABSL_IS_BIG_ENDIAN)
  auto flip_order_for_endianness = [](const absl::uint128& input) {
    return absl::MakeUint128(__builtin_bswap64(absl::Uint128Low64(input)),
                             __builtin_bswap64(absl::Uint128High64(input)));
  };
#else
  auto flip_order_for_endianness = [](const absl::uint128& input) { return input; };
#endif
  {
    Address::Ipv6Instance address("::1");
    uint64_t high = 0x100000000000000;
    EXPECT_EQ(flip_order_for_endianness(absl::MakeUint128(high, 0)),
              address.ip()->ipv6()->address());
    EXPECT_EQ(flip_order_for_endianness(absl::MakeUint128(high, 0)),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));

    EXPECT_EQ(absl::uint128(1), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    Address::Ipv6Instance address("1::");
    EXPECT_EQ(flip_order_for_endianness(absl::uint128(256)), address.ip()->ipv6()->address());
    EXPECT_EQ(flip_order_for_endianness(absl::uint128(256)),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));

    uint64_t high = 0x001000000000000;
    EXPECT_EQ(absl::MakeUint128(high, 0), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    Address::Ipv6Instance address("2001:abcd:ef01:2345:6789:abcd:ef01:234");
    uint64_t low = 0x452301EFCDAB0120;
    uint64_t high = 0x340201EFCDAB8967;
    EXPECT_EQ(flip_order_for_endianness(absl::MakeUint128(high, low)),
              address.ip()->ipv6()->address());
    EXPECT_EQ(flip_order_for_endianness(absl::MakeUint128(high, low)),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));
  }
  {
    Address::Ipv6Instance address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    EXPECT_EQ(absl::Uint128Max(), address.ip()->ipv6()->address());
    EXPECT_EQ(absl::Uint128Max(), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    TestRandomGenerator rand;
    absl::uint128 random_number = absl::MakeUint128(rand.random(), rand.random());
    EXPECT_EQ(random_number, Utility::Ip6htonl(Utility::Ip6ntohl(random_number)));
  }
}

TEST(ResolvedUdpSocketConfig, Warning) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro()).WillOnce(Return(false));
  EXPECT_LOG_CONTAINS(
      "warn", "GRO requested but not supported by the OS. Check OS config or disable prefer_gro.",
      ResolvedUdpSocketConfig resolved_config(envoy::config::core::v3::UdpSocketConfig(), true));
}

#ifndef WIN32
TEST(PacketLoss, LossTest) {
  // Create and bind a UDP socket.
  auto version = TestEnvironment::getIpVersionsForTest()[0];
  auto kernel_version = version == Network::Address::IpVersion::v4 ? AF_INET : AF_INET6;
  int fd = socket(kernel_version, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
  ASSERT_NE(fd, 0);
  sockaddr_storage storage;
  auto& sin = reinterpret_cast<sockaddr_in&>(storage);
  sin.sin_family = kernel_version;
  sin.sin_port = 0;
  EXPECT_EQ(1,
            inet_pton(kernel_version, Network::Test::getLoopbackAddressUrlString(version).c_str(),
                      &sin.sin_addr));
  ASSERT_EQ(0, bind(fd, reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Get the port.
  socklen_t storage_len = sizeof(storage);
  ASSERT_EQ(0, getsockname(fd, reinterpret_cast<sockaddr*>(&storage), &storage_len));

  // Set the buffer size artificially small.
  int receive_buffer_size = 1000;
  ASSERT_EQ(
      0, setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, sizeof(receive_buffer_size)));

  // Send a packet.
  char buf[2048];
  memset(buf, 0, ABSL_ARRAYSIZE(buf));
  EXPECT_EQ(ABSL_ARRAYSIZE(buf), sendto(fd, buf, ABSL_ARRAYSIZE(buf), 0,
                                        reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Verify the packet is dropped.
  IoSocketHandleImpl handle(fd);
  auto address = Network::Test::getCanonicalLoopbackAddress(version);
  NiceMock<MockUdpPacketProcessor> processor;
  IoHandle::UdpSaveCmsgConfig udp_save_cmsg_config;
  ON_CALL(processor, saveCmsgConfig()).WillByDefault(ReturnRef(udp_save_cmsg_config));
  MonotonicTime time(std::chrono::seconds(0));
  uint32_t packets_dropped = 0;
  UdpRecvMsgMethod recv_msg_method = UdpRecvMsgMethod::RecvMsg;
  if (Api::OsSysCallsSingleton::get().supportsMmsg()) {
    recv_msg_method = UdpRecvMsgMethod::RecvMmsg;
  }

  uint32_t packets_read = 0;
  Utility::readFromSocket(handle, *address, processor, time, recv_msg_method, &packets_dropped,
                          &packets_read);
  EXPECT_EQ(1, packets_dropped);
  EXPECT_EQ(0, packets_read);

  // Send another packet.
  EXPECT_EQ(ABSL_ARRAYSIZE(buf), sendto(fd, buf, ABSL_ARRAYSIZE(buf), 0,
                                        reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Make sure the drop count is now 2.
  Utility::readFromSocket(handle, *address, processor, time, recv_msg_method, &packets_dropped,
                          &packets_read);
  EXPECT_EQ(2, packets_dropped);
  EXPECT_EQ(0, packets_read);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy
