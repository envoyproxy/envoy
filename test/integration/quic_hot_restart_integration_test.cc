#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/network/parent_drained_callback_registrar.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/server/hot_restart_nop_impl.h"

#include "test/common/quic/test_utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace {

// Plays the hot restart parent for a server under test: hands it a UDP listen socket the test
// owns, as a parent passes its own, keeps the server's QUIC listener paused until the test says the
// parent has drained, and forwards packets to the listener the way a parent forwards the packets
// that are not for its connections.
class TestHotRestartParent : public Server::HotRestartNopImpl,
                             public Network::ParentDrainedCallbackRegistrar {
public:
  explicit TestHotRestartParent(int udp_fd) : udp_fd_(udp_fd) {}

  // Server::HotRestart
  bool parentStopAcceptingRequested() override { return false; }
  int duplicateParentListenSocket(const std::string& address, uint32_t,
                                  absl::string_view) override {
    if (!absl::StartsWith(address, Network::Utility::UDP_SCHEME)) {
      return -1;
    }
    return ::dup(udp_fd_);
  }
  void registerUdpForwardingListener(
      Network::Address::InstanceConstSharedPtr address,
      std::shared_ptr<Network::UdpListenerConfig> listener_config) override {
    absl::MutexLock lock(&mutex_);
    forwarding_address_ = address;
    forwarding_listener_ = listener_config;
  }
  OptRef<Network::ParentDrainedCallbackRegistrar> parentDrainedCallbackRegistrar() override {
    return *this;
  }

  // Network::ParentDrainedCallbackRegistrar
  void registerParentDrainedCallback(const Network::Address::InstanceConstSharedPtr&,
                                     absl::AnyInvocable<void()> callback) override {
    absl::MutexLock lock(&mutex_);
    drained_callbacks_.push_back(std::move(callback));
  }

  size_t pausedListeners() {
    absl::MutexLock lock(&mutex_);
    return drained_callbacks_.size();
  }

  // Delivers a datagram to worker 0 of the server's listener, as a forwarded packet.
  void forward(Network::Address::InstanceConstSharedPtr peer, const Buffer::Instance& payload,
               MonotonicTime receive_time) {
    absl::MutexLock lock(&mutex_);
    ASSERT_NE(forwarding_listener_, nullptr);
    Network::UdpRecvData data;
    data.addresses_.local_ = forwarding_address_;
    data.addresses_.peer_ = std::move(peer);
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(payload);
    data.receive_time_ = receive_time;
    forwarding_listener_->listenerWorkerRouter(*forwarding_address_).deliver(0, std::move(data));
  }

  // The parent is gone: the server's listeners take over the socket.
  void drained() {
    std::vector<absl::AnyInvocable<void()>> callbacks;
    {
      absl::MutexLock lock(&mutex_);
      callbacks.swap(drained_callbacks_);
    }
    for (auto& callback : callbacks) {
      callback();
    }
  }

private:
  const int udp_fd_;
  absl::Mutex mutex_;
  Network::Address::InstanceConstSharedPtr forwarding_address_ ABSL_GUARDED_BY(mutex_);
  std::shared_ptr<Network::UdpListenerConfig> forwarding_listener_ ABSL_GUARDED_BY(mutex_);
  std::vector<absl::AnyInvocable<void()>> drained_callbacks_ ABSL_GUARDED_BY(mutex_);
};

// A bound UDP socket owned by the test.
class TestUdpSocket {
public:
  explicit TestUdpSocket(Network::Address::IpVersion version, bool reuse_port = false) {
    const int family = version == Network::Address::IpVersion::v4 ? AF_INET : AF_INET6;
    // Non-blocking, as a parent's listen socket is: the server reads its duplicate until EAGAIN.
    fd_ = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    RELEASE_ASSERT(fd_ >= 0, "");
    if (reuse_port) {
      const int on = 1;
      RELEASE_ASSERT(::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) == 0, "");
    }
    auto loopback = Network::Test::getCanonicalLoopbackAddress(version);
    RELEASE_ASSERT(::bind(fd_, loopback->sockAddr(), loopback->sockAddrLen()) == 0, "");
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    RELEASE_ASSERT(::getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &len) == 0, "");
    address_ = *Network::Address::addressFromSockAddr(ss, len);
  }
  ~TestUdpSocket() { ::close(fd_); }

  int fd() const { return fd_; }
  const Network::Address::InstanceConstSharedPtr& address() const { return address_; }

  void sendTo(const Network::Address::Instance& to, absl::string_view payload) {
    ASSERT_EQ(::sendto(fd_, payload.data(), payload.size(), 0, to.sockAddr(), to.sockAddrLen()),
              static_cast<ssize_t>(payload.size()));
  }

  // Receives a datagram, waiting up to timeout; nullopt if none arrived. Records the sender.
  std::optional<std::string> receive(std::chrono::milliseconds timeout,
                                     Network::Address::InstanceConstSharedPtr* from = nullptr) {
    pollfd pfd{fd_, POLLIN, 0};
    if (::poll(&pfd, 1, static_cast<int>(timeout.count())) != 1) {
      return std::nullopt;
    }
    char buf[2048];
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    const ssize_t n =
        ::recvfrom(fd_, buf, sizeof(buf), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&ss), &len);
    if (n < 0) {
      return std::nullopt;
    }
    if (from != nullptr) {
      *from = *Network::Address::addressFromSockAddr(ss, len);
    }
    return std::string(buf, n);
  }

private:
  int fd_;
  Network::Address::InstanceConstSharedPtr address_;
};

// A QUIC packet with a short header for a connection the server does not know, the kind of packet
// a parent's connection sends: a server that reads it answers with a stateless reset.
std::string parentConnectionPacket() {
  std::string packet(64, 'p');
  packet[0] = 0x40;
  return packet;
}

class QuicHotRestartIntegrationTest : public HttpIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHotRestartIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP3, GetParam(),
                            ConfigHelper::quicHttpProxyConfig()),
        listen_socket_(GetParam(), /*reuse_port=*/true), parent_(listen_socket_.fd()) {
    hot_restart_ = &parent_;
    const uint32_t port = listen_socket_.address()->ip()->port();
    config_helper_.addConfigModifier([port](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()
          ->mutable_listeners(0)
          ->mutable_address()
          ->mutable_socket_address()
          ->set_port_value(port);
    });
  }

  ~QuicHotRestartIntegrationTest() override {
    // The server holds parent_ as its hot restart implementation: stop it first.
    test_server_.reset();
  }

  // Forwards a client's first flight (a new connection) to the server, as from `peer`.
  void forwardNewConnection(const TestUdpSocket& peer, uint64_t connection_id) {
    quic::QuicConfig quic_config;
    for (const auto& packet :
         Quic::generateChloPacketsToSend(quic::CurrentSupportedHttp3Versions()[0], quic_config,
                                         quic::test::TestConnectionId(connection_id))) {
      parent_.forward(peer.address(), packet, timeSystem().monotonicTime());
    }
  }

  // The listen socket the "parent" hands the server, and that both of them hold.
  TestUdpSocket listen_socket_;
  TestHotRestartParent parent_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicHotRestartIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// While its parent drains, a QUIC listener on an inherited socket must leave the socket to the
// parent: the parent reads every packet and forwards those that are not for its own connections,
// and anything the listener dequeued itself would be a packet of one of the parent's connections,
// which it would answer with a stateless reset. Handling the forwarded new connections must not
// make the listener read the socket.
TEST_P(QuicHotRestartIntegrationTest, PausedListenerLeavesTheParentsPacketsOnTheSocket) {
  initialize();
  ASSERT_EQ(parent_.pausedListeners(), 1);

  // A packet of one of the parent's connections waits on the shared socket for the parent.
  TestUdpSocket parent_peer(GetParam());
  parent_peer.sendTo(*listen_socket_.address(), parentConnectionPacket());

  // The parent forwards a new connection; the server buffers its handshake and processes it on
  // its next pass over the listener, answering the client.
  TestUdpSocket client(GetParam());
  forwardNewConnection(client, 1);
  ASSERT_TRUE(client.receive(TestUtility::DefaultTimeout).has_value());
  // And once more, so that the pass that answered the first one has completed.
  forwardNewConnection(client, 2);
  ASSERT_TRUE(client.receive(TestUtility::DefaultTimeout).has_value());
  test_server_->waitForWorkerThreads();

  // The parent's packet is still on the socket for the parent, and the parent's peer has not been
  // reset.
  Network::Address::InstanceConstSharedPtr from;
  const std::optional<std::string> queued =
      listen_socket_.receive(std::chrono::milliseconds(0), &from);
  ASSERT_TRUE(queued.has_value());
  EXPECT_EQ(*queued, parentConnectionPacket());
  EXPECT_EQ(from->asString(), parent_peer.address()->asString());
  EXPECT_FALSE(parent_peer.receive(std::chrono::milliseconds(0)).has_value());

  // Once the parent has drained the server reads the socket itself: the same packet now reaches
  // the server, which does not know the connection and resets it.
  parent_.drained();
  parent_peer.sendTo(*listen_socket_.address(), parentConnectionPacket());
  EXPECT_TRUE(parent_peer.receive(TestUtility::DefaultTimeout).has_value());
}

} // namespace
} // namespace Envoy
