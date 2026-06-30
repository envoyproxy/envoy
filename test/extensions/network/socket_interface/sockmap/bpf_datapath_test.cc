#include <cstddef>
#include <cstring>

#include "envoy/common/platform.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/network/socket_interface/sockmap/bpf_datapath.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

TEST(BuildSockKey, Ipv4MapsTupleAndConvertsPorts) {
  Address::Ipv4Instance local("1.2.3.4", 1111);
  Address::Ipv4Instance peer("5.6.7.8", 2222);

  SockKey key;
  ASSERT_TRUE(buildSockKey(local, peer, key));

  EXPECT_EQ(key.sip4, local.ip()->ipv4()->address());
  EXPECT_EQ(key.dip4, peer.ip()->ipv4()->address());
  EXPECT_EQ(key.family, 1);
  EXPECT_EQ(key.pad1, 0);
  EXPECT_EQ(key.pad2, 0);
  // Ports are network order in the low 16 bits, matching the eBPF program.
  EXPECT_EQ(key.sport, htons(1111));
  EXPECT_EQ(key.dport, htons(2222));
}

TEST(BuildSockKey, RejectsIpv6) {
  Address::Ipv6Instance v6("::1", 1111);
  Address::Ipv4Instance v4("1.2.3.4", 2222);

  SockKey key;
  EXPECT_FALSE(buildSockKey(v6, v4, key));
  EXPECT_FALSE(buildSockKey(v4, v6, key));
  EXPECT_FALSE(buildSockKey(v6, v6, key));
}

TEST(BuildSockKey, RejectsPipe) {
  auto pipe = *Address::PipeInstance::create("/tmp/sockmap_test.sock");
  Address::Ipv4Instance v4("1.2.3.4", 2222);

  SockKey key;
  EXPECT_FALSE(buildSockKey(*pipe, v4, key));
  EXPECT_FALSE(buildSockKey(v4, *pipe, key));
}

// The key crosses into the kernel, so both its layout and its byte values must match struct
// sock_key in `bpf/sockmap_kern.c`. The eBPF side cannot run in unprivileged CI, so the offsets
// guard against layout drift and the wire image guards against an encoding mismatch such as the
// htonl vs htons port byte order.
TEST(SockKey, LayoutMatchesKernelContract) {
  EXPECT_EQ(sizeof(SockKey), 20u);
  EXPECT_EQ(offsetof(SockKey, sip4), 0u);
  EXPECT_EQ(offsetof(SockKey, dip4), 4u);
  EXPECT_EQ(offsetof(SockKey, family), 8u);
  EXPECT_EQ(offsetof(SockKey, pad1), 9u);
  EXPECT_EQ(offsetof(SockKey, pad2), 10u);
  EXPECT_EQ(offsetof(SockKey, sport), 12u);
  EXPECT_EQ(offsetof(SockKey, dport), 16u);

  // A key built for 1.2.3.4:1111 -> 5.6.7.8:2222 must serialize to this exact wire image on the
  // little-endian targets Envoy supports.
  Address::Ipv4Instance local("1.2.3.4", 1111);
  Address::Ipv4Instance peer("5.6.7.8", 2222);
  SockKey key;
  ASSERT_TRUE(buildSockKey(local, peer, key));
  const uint8_t expected[sizeof(SockKey)] = {
      0x01, 0x02, 0x03, 0x04, // sip4 1.2.3.4 network order
      0x05, 0x06, 0x07, 0x08, // dip4 5.6.7.8 network order
      0x01, 0x00, 0x00, 0x00, // family 1, pads
      0x04, 0x57, 0x00, 0x00, // source port htons(1111) in the low 16 bits
      0x08, 0xae, 0x00, 0x00, // destination port htons(2222) in the low 16 bits
  };
  EXPECT_EQ(0, std::memcmp(&key, expected, sizeof(SockKey)));
}

// Without `ENVOY_ENABLE_SOCKMAP` the datapath is a no-op, so every configuration falls back to the
// standard datapath by returning a null datapath.
TEST(CreateBpfDatapath, ReturnsNullWhenNotCompiledIn) {
  BpfDatapathConfig config;
  config.sockhash_max_entries = 1024;

  EXPECT_EQ(createBpfDatapath(config), nullptr);

  config.bpf_program_path = "/does/not/exist.o";
  EXPECT_EQ(createBpfDatapath(config), nullptr);
}

} // namespace
} // namespace Network
} // namespace Envoy
