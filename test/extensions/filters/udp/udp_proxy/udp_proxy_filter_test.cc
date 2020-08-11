#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"

#include "common/network/socket_impl.h"
#include "common/network/socket_option_impl.h"

#include "extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "test/mocks/network/io_handle.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::ByMove;
using testing::InSequence;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnNew;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace {

class MockSocket : public Network::Socket {
public:
  MockSocket() : io_handle_(std::make_unique<Network::MockIoHandle>()){};
  ~MockSocket() override = default;

  Network::IoHandle& ioHandle() override { return *io_handle_; };

  const Network::IoHandle& ioHandle() const override { return *io_handle_; };

  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t len) override {
    return io_handle_->setOption(level, optname, optval, len);
  }

  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(void, setLocalAddress, (const Network::Address::InstanceConstSharedPtr&), (override));
  MOCK_METHOD(Network::Socket::Type, socketType, (), (const, override));
  MOCK_METHOD(Network::Address::Type, addressType, (), (const, override));
  MOCK_METHOD(absl::optional<Network::Address::IpVersion>, ipVersion, (), (const, override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(const OptionsSharedPtr&, options, (), (const, override));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Network::Address::InstanceConstSharedPtr),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Network::Address::InstanceConstSharedPtr),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int), (override));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*),
              (const, override));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool), (override));
  MOCK_METHOD(void, addOption, (const Network::Socket::OptionConstSharedPtr&), (override));
  MOCK_METHOD(void, addOptions, (const Network::Socket::OptionsSharedPtr&), (override));

  const std::unique_ptr<Network::MockIoHandle> io_handle_;
};

class TestUdpProxyFilter : public UdpProxyFilter {
public:
  using UdpProxyFilter::UdpProxyFilter;

  MOCK_METHOD(Network::SocketPtr, createSocket, (const Upstream::HostConstSharedPtr& host));
};

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.rc_ = rc;
  return no_error;
}

Api::SysCallIntResult makeNoError() {
  Api::SysCallIntResult no_error;
  no_error.rc_ = 0;
  no_error.errno_ = 0;
  return no_error;
}

Api::IoCallUint64Result makeError(int sys_errno) {
  return Api::IoCallUint64Result(0, Api::IoErrorPtr(new Network::IoSocketError(sys_errno),
                                                    Network::IoSocketError::deleteIoError));
}

class UdpProxyFilterTest : public testing::Test {
public:
  struct TestSession {
    TestSession(UdpProxyFilterTest& parent,
                const Network::Address::InstanceConstSharedPtr& upstream_address)
        : parent_(parent), upstream_address_(upstream_address), socket_(new MockSocket()) {}

    void expectUpstreamWriteOriginalSrcIp() {
      EXPECT_CALL(*socket_->io_handle_, setOption(_, _, _, _))
          .WillRepeatedly(Invoke([this](int level, int optname, const void* optval,
                                        socklen_t) -> Api::SysCallIntResult {
            sock_opts_[level][optname] = *reinterpret_cast<const int*>(optval);
            return makeNoError();
          }));
    }

    void expectUpstreamWrite(const std::string& data, int sys_errno = 0,
                             const Network::Address::Ip* local_ip = nullptr) {
      EXPECT_CALL(*idle_timer_, enableTimer(parent_.config_->sessionTimeout(), nullptr));
      EXPECT_CALL(*socket_->io_handle_, sendmsg(_, 1, 0, _, _))
          .WillOnce(Invoke(
              [this, data, local_ip, sys_errno](
                  const Buffer::RawSlice* slices, uint64_t, int,
                  const Network::Address::Ip* self_ip,
                  const Network::Address::Instance& peer_address) -> Api::IoCallUint64Result {
                EXPECT_EQ(data, absl::string_view(static_cast<const char*>(slices[0].mem_),
                                                  slices[0].len_));
                EXPECT_EQ(peer_address, *upstream_address_);
                if (self_ip && local_ip) {
                  EXPECT_EQ(self_ip->addressAsString(), local_ip->addressAsString());
                }
                return sys_errno == 0 ? makeNoError(data.size()) : makeError(sys_errno);
              }));
    }

    void recvDataFromUpstream(const std::string& data, int recv_sys_errno = 0,
                              int send_sys_errno = 0) {
      EXPECT_CALL(*idle_timer_, enableTimer(parent_.config_->sessionTimeout(), nullptr));

      EXPECT_CALL(*socket_->io_handle_, supportsUdpGro());
      EXPECT_CALL(*socket_->io_handle_, supportsMmsg());
      // Return the datagram.
      EXPECT_CALL(*socket_->io_handle_, recvmsg(_, 1, _, _))
          .WillOnce(
              Invoke([this, data, recv_sys_errno](
                         Buffer::RawSlice* slices, const uint64_t, uint32_t,
                         Network::IoHandle::RecvMsgOutput& output) -> Api::IoCallUint64Result {
                if (recv_sys_errno != 0) {
                  return makeError(recv_sys_errno);
                } else {
                  ASSERT(data.size() <= slices[0].len_);
                  memcpy(slices[0].mem_, data.data(), data.size());
                  output.msg_[0].peer_address_ = upstream_address_;
                  return makeNoError(data.size());
                }
              }));
      if (recv_sys_errno == 0) {
        // Send the datagram downstream.
        EXPECT_CALL(parent_.callbacks_.udp_listener_, send(_))
            .WillOnce(Invoke([data, send_sys_errno](
                                 const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
              // TODO(mattklein123): Verify peer/local address.
              EXPECT_EQ(send_data.buffer_.toString(), data);
              if (send_sys_errno == 0) {
                send_data.buffer_.drain(send_data.buffer_.length());
                return makeNoError(data.size());
              } else {
                return makeError(send_sys_errno);
              }
            }));
        // Return an EAGAIN result.
        EXPECT_CALL(*socket_->io_handle_, supportsUdpGro());
        EXPECT_CALL(*socket_->io_handle_, supportsMmsg());
        EXPECT_CALL(*socket_->io_handle_, recvmsg(_, 1, _, _))
            .WillOnce(Return(ByMove(Api::IoCallUint64Result(
                0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                   Network::IoSocketError::deleteIoError)))));
      }

      // Kick off the receive.
      file_event_cb_(Event::FileReadyType::Read);
    }

    UdpProxyFilterTest& parent_;
    const Network::Address::InstanceConstSharedPtr upstream_address_;
    Event::MockTimer* idle_timer_{};
    MockSocket* socket_;
    std::map<int, std::map<int, int>> sock_opts_;
    Event::FileReadyCb file_event_cb_;
  };

  UdpProxyFilterTest()
      : UdpProxyFilterTest(Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000")) {}

  explicit UdpProxyFilterTest(Network::Address::InstanceConstSharedPtr&& peer_address)
      : upstream_address_(Network::Utility::parseInternetAddressAndPort("20.0.0.1:443")),
        peer_address_(std::move(peer_address)) {
    // Disable strict mock warnings.
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(*cluster_manager_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(upstream_address_));
    EXPECT_CALL(*cluster_manager_.thread_local_cluster_.lb_.host_, health())
        .WillRepeatedly(Return(Upstream::Host::Health::Healthy));
  }

  ~UdpProxyFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const std::string& yaml, bool has_cluster = true) {
    envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    config_ = std::make_shared<UdpProxyFilterConfig>(cluster_manager_, time_system_, stats_store_,
                                                     config);
    EXPECT_CALL(cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (has_cluster) {
      EXPECT_CALL(cluster_manager_, get(_));
    } else {
      EXPECT_CALL(cluster_manager_, get(_)).WillOnce(Return(nullptr));
    }
    filter_ = std::make_unique<TestUdpProxyFilter>(callbacks_, config_);
  }

  void recvDataFromDownstream(const std::string& peer_address, const std::string& local_address,
                              const std::string& buffer) {
    Network::UdpRecvData data;
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(peer_address);
    data.addresses_.local_ = Network::Utility::parseInternetAddressAndPort(local_address);
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  void expectSessionCreate(const Network::Address::InstanceConstSharedPtr& address) {
    test_sessions_.emplace_back(*this, address);
    TestSession& new_session = test_sessions_.back();
    new_session.idle_timer_ = new Event::MockTimer(&callbacks_.udp_listener_.dispatcher_);
    EXPECT_CALL(*filter_, createSocket(_))
        .WillOnce(Return(ByMove(Network::SocketPtr{test_sessions_.back().socket_})));
    EXPECT_CALL(*new_session.socket_->io_handle_, fd());
    EXPECT_CALL(
        callbacks_.udp_listener_.dispatcher_,
        createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
        .WillOnce(DoAll(SaveArg<1>(&new_session.file_event_cb_), Return(nullptr)));
    // Internal Buffer is Empty, flush will be a no-op
    ON_CALL(callbacks_.udp_listener_, flush())
        .WillByDefault(
            InvokeWithoutArgs([]() -> Api::IoCallUint64Result { return makeNoError(0); }));
  }

  void checkTransferStats(uint64_t rx_bytes, uint64_t rx_datagrams, uint64_t tx_bytes,
                          uint64_t tx_datagrams) {
    EXPECT_EQ(rx_bytes, config_->stats().downstream_sess_rx_bytes_.value());
    EXPECT_EQ(rx_datagrams, config_->stats().downstream_sess_rx_datagrams_.value());
    EXPECT_EQ(tx_bytes, config_->stats().downstream_sess_tx_bytes_.value());
    EXPECT_EQ(tx_datagrams, config_->stats().downstream_sess_tx_datagrams_.value());
  }

  Upstream::MockClusterManager cluster_manager_;
  NiceMock<MockTimeSystem> time_system_;
  Stats::IsolatedStoreImpl stats_store_;
  UdpProxyFilterConfigSharedPtr config_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks_{};
  std::unique_ptr<TestUdpProxyFilter> filter_;
  std::vector<TestSession> test_sessions_;
  const Network::Address::InstanceConstSharedPtr upstream_address_;
  const Network::Address::InstanceConstSharedPtr peer_address_;
};

class UdpProxyFilterTestIpv6 : public UdpProxyFilterTest {
public:
  UdpProxyFilterTestIpv6()
      : UdpProxyFilterTestIpv6(
            Network::Utility::parseInternetAddressAndPort("[2001:db8:85a3::8a2e:370:7334]:443")) {}

  explicit UdpProxyFilterTestIpv6(Network::Address::InstanceConstSharedPtr&& address)
      : UdpProxyFilterTest(
            Network::Utility::parseInternetAddressAndPort("[2001:db8:85a3::9a2e:370:7334]:1000")),
        upstream_address_v6_(std::move(address)) {
    EXPECT_CALL(*cluster_manager_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(upstream_address_v6_));
  }

  const Network::Address::InstanceConstSharedPtr upstream_address_v6_;
};

class UdpProxyFilterTestIpv4Ipv6 : public UdpProxyFilterTestIpv6 {
public:
  UdpProxyFilterTestIpv4Ipv6()
      : UdpProxyFilterTestIpv6(Network::Utility::parseInternetAddressAndPort(
            "[2001:db8:85a3::8a2e:370:7334]:443", false)) {}
};

// Basic UDP proxy flow with a single session.
TEST_F(UdpProxyFilterTest, BasicFlow) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  test_sessions_[0].expectUpstreamWrite("hello2");
  test_sessions_[0].expectUpstreamWrite("hello3");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  checkTransferStats(11 /*rx_bytes*/, 2 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello3");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world2");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 11 /*tx_bytes*/, 2 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world3");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 17 /*tx_bytes*/, 3 /*tx_datagrams*/);
}

// Idle timeout flow.
TEST_F(UdpProxyFilterTest, IdleTimeout) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Verify downstream send and receive error handling.
TEST_F(UdpProxyFilterTest, SendReceiveErrorHandling) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  filter_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
  EXPECT_EQ(1, config_->stats().downstream_sess_rx_errors_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(5, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_tx_bytes_total_.value());

  test_sessions_[0].recvDataFromUpstream("world2", 0, SOCKET_ERROR_MSG_SIZE);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_tx_errors_.value());

  test_sessions_[0].recvDataFromUpstream("world2", SOCKET_ERROR_MSG_SIZE, 0);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(1, TestUtility::findCounter(
                   cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
                   "udp.sess_rx_errors")
                   ->value());

  test_sessions_[0].expectUpstreamWrite("hello", SOCKET_ERROR_MSG_SIZE);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  checkTransferStats(10 /*rx_bytes*/, 2 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(5, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_tx_bytes_total_.value());
  EXPECT_EQ(1, TestUtility::findCounter(
                   cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
                   "udp.sess_tx_errors")
                   ->value());
}

// No upstream host handling.
TEST_F(UdpProxyFilterTest, NoUpstreamHost) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  EXPECT_CALL(cluster_manager_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_none_healthy_.value());
}

// No cluster at filter creation.
TEST_F(UdpProxyFilterTest, NoUpstreamClusterAtCreation) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF",
        false);

  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_no_route_.value());
}

// Dynamic cluster addition and removal handling.
TEST_F(UdpProxyFilterTest, ClusterDynamicAddAndRemoval) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF",
        false);

  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_no_route_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  // Add a cluster that we don't care about.
  NiceMock<Upstream::MockThreadLocalCluster> other_thread_local_cluster;
  other_thread_local_cluster.cluster_.info_->name_ = "other_cluster";
  cluster_update_callbacks_->onClusterAddOrUpdate(other_thread_local_cluster);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_no_route_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  // Now add the cluster we care about.
  cluster_update_callbacks_->onClusterAddOrUpdate(cluster_manager_.thread_local_cluster_);
  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // Remove a cluster we don't care about.
  cluster_update_callbacks_->onClusterRemoval("other_cluster");
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // Remove the cluster we do care about. This should purge all sessions.
  cluster_update_callbacks_->onClusterRemoval("fake_cluster");
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
}

// Hitting the maximum per-cluster connection/session circuit breaker.
TEST_F(UdpProxyFilterTest, MaxSessionsCircuitBreaker) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  // Allow only a single session.
  cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(1, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // This should hit the session circuit breaker.
  recvDataFromDownstream("10.0.0.2:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(
      1,
      cluster_manager_.thread_local_cluster_.cluster_.info_->stats_.upstream_cx_overflow_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // Timing out the 1st session should allow us to create another.
  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.2:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Verify that all sessions for a host are removed when a host is removed.
TEST_F(UdpProxyFilterTest, RemoveHostSessions) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  cluster_manager_.thread_local_cluster_.cluster_.priority_set_.runUpdateCallbacks(
      0, {}, {cluster_manager_.thread_local_cluster_.lb_.host_});
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// In this case the host becomes unhealthy, but we get the same host back, so just keep using the
// current session.
TEST_F(UdpProxyFilterTest, HostUnhealthyPickSameHost) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  EXPECT_CALL(*cluster_manager_.thread_local_cluster_.lb_.host_, health())
      .WillRepeatedly(Return(Upstream::Host::Health::Unhealthy));
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
}

// Make sure that we are able to create a new session if there is an available healthy host and
// our current host is unhealthy.
TEST_F(UdpProxyFilterTest, HostUnhealthyPickDifferentHost) {
  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
  )EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  EXPECT_CALL(*cluster_manager_.thread_local_cluster_.lb_.host_, health())
      .WillRepeatedly(Return(Upstream::Host::Health::Unhealthy));
  auto new_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto new_host_address = Network::Utility::parseInternetAddressAndPort("20.0.0.2:443");
  ON_CALL(*new_host, address()).WillByDefault(Return(new_host_address));
  ON_CALL(*new_host, health()).WillByDefault(Return(Upstream::Host::Health::Healthy));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(new_host));
  expectSessionCreate(new_host_address);
  test_sessions_[1].expectUpstreamWrite("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Make sure socket option is set correctly if use_original_src_ip is set.
TEST_F(UdpProxyFilterTest, SocketOptionForUseOriginalSrcIp) {
  const auto ip_trans = ENVOY_SOCKET_IP_TRANSPARENT;
  const auto ipv6_trans = ENVOY_SOCKET_IPV6_TRANSPARENT;

  if (!(ip_trans.hasValue() && ipv6_trans.hasValue())) {
    // The options are not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
use_original_src_ip: true
)EOF");

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectUpstreamWriteOriginalSrcIp();
  test_sessions_[0].expectUpstreamWrite("hello", 0, peer_address_->ip());
  recvDataFromDownstream(peer_address_->asString(), "10.0.0.2:80", "hello");

  EXPECT_EQ(1, test_sessions_[0].sock_opts_[ip_trans.level()][ip_trans.option()]);
  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ipv6_trans.level()][ipv6_trans.option()]);
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

// Make sure socket option is set correctly if use_original_src_ip is set in case of ipv6.
TEST_F(UdpProxyFilterTestIpv6, SocketOptionForUseOriginalSrcIpInCaseOfIpv6) {
  const auto ip_trans = ENVOY_SOCKET_IP_TRANSPARENT;
  const auto ipv6_trans = ENVOY_SOCKET_IPV6_TRANSPARENT;

  if (!(ip_trans.hasValue() && ipv6_trans.hasValue())) {
    // The options are not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
use_original_src_ip: true
)EOF");

  expectSessionCreate(upstream_address_v6_);
  test_sessions_[0].expectUpstreamWriteOriginalSrcIp();
  test_sessions_[0].expectUpstreamWrite("hello", 0, peer_address_->ip());
  recvDataFromDownstream(peer_address_->asString(), "[2001:db8:85a3::9a2e:370:7335]:80", "hello");

  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ip_trans.level()][ip_trans.option()]);
  EXPECT_EQ(1, test_sessions_[0].sock_opts_[ipv6_trans.level()][ipv6_trans.option()]);
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

// Make sure socket option is set correctly if use_original_src_ip is set in case of ipv4+ipv6.
TEST_F(UdpProxyFilterTestIpv4Ipv6, SocketOptionForUseOriginalSrcIpInCaseOfIpv4Ipv6) {
  const auto ip_trans = ENVOY_SOCKET_IP_TRANSPARENT;
  const auto ipv6_trans = ENVOY_SOCKET_IPV6_TRANSPARENT;

  if (!(ip_trans.hasValue() && ipv6_trans.hasValue())) {
    // The options are not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
use_original_src_ip: true
)EOF");

  expectSessionCreate(upstream_address_v6_);
  test_sessions_[0].expectUpstreamWriteOriginalSrcIp();
  test_sessions_[0].expectUpstreamWrite("hello", 0, peer_address_->ip());
  recvDataFromDownstream(peer_address_->asString(), "[2001:db8:85a3::9a2e:370:7335]:80", "hello");

  EXPECT_EQ(1, test_sessions_[0].sock_opts_[ip_trans.level()][ip_trans.option()]);
  EXPECT_EQ(1, test_sessions_[0].sock_opts_[ipv6_trans.level()][ipv6_trans.option()]);
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

// Make sure socket option is not set correctly if use_original_src_ip is not set.
TEST_F(UdpProxyFilterTestIpv4Ipv6, NoSocketOptionForDontUseOriginalSrcIp) {
  const auto ip_trans = ENVOY_SOCKET_IP_TRANSPARENT;
  const auto ipv6_trans = ENVOY_SOCKET_IPV6_TRANSPARENT;

  if (!(ip_trans.hasValue() && ipv6_trans.hasValue())) {
    // The options are not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
use_original_src_ip: false
)EOF");

  expectSessionCreate(upstream_address_v6_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("[2001:db8:85a3::9a2e:370:7334]:1000", "[2001:db8:85a3::9a2e:370:7335]:80",
                         "hello");

  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ip_trans.level()][ip_trans.option()]);
  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ipv6_trans.level()][ipv6_trans.option()]);
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

// Make sure socket option is not set correctly if use_original_src_ip is not mentioned.
TEST_F(UdpProxyFilterTestIpv4Ipv6, NoSocketOptionForNoUseOriginalSrcIp) {
  const auto ip_trans = ENVOY_SOCKET_IP_TRANSPARENT;
  const auto ipv6_trans = ENVOY_SOCKET_IPV6_TRANSPARENT;

  if (!(ip_trans.hasValue() && ipv6_trans.hasValue())) {
    // The options are not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(R"EOF(
stat_prefix: foo
cluster: fake_cluster
)EOF");

  expectSessionCreate(upstream_address_v6_);
  test_sessions_[0].expectUpstreamWrite("hello");
  recvDataFromDownstream("[2001:db8:85a3::9a2e:370:7334]:1000", "[2001:db8:85a3::9a2e:370:7335]:80",
                         "hello");

  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ip_trans.level()][ip_trans.option()]);
  EXPECT_EQ(0, test_sessions_[0].sock_opts_[ipv6_trans.level()][ipv6_trans.option()]);
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

} // namespace
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
