#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"

#include "source/common/common/hash.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/socket.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::ByMove;
using testing::DoAll;
using testing::DoDefault;
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

class TestUdpProxyFilter : public UdpProxyFilter {
public:
  using UdpProxyFilter::UdpProxyFilter;

  MOCK_METHOD(Network::SocketPtr, createSocket, (const Upstream::HostConstSharedPtr& host));
};

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.return_value_ = rc;
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
        : parent_(parent), upstream_address_(upstream_address),
          socket_(new NiceMock<Network::MockSocket>()) {
      ON_CALL(*socket_, ipVersion()).WillByDefault(Return(upstream_address_->ip()->version()));
    }

    void expectSetIpTransparentSocketOption() {
      EXPECT_CALL(*socket_->io_handle_, setOption(_, _, _, _))
          .WillRepeatedly(Invoke([this](int level, int optname, const void* optval,
                                        socklen_t) -> Api::SysCallIntResult {
            sock_opts_[level][optname] = *reinterpret_cast<const int*>(optval);
            return Api::SysCallIntResult{0, 0};
          }));
    }

    void expectWriteToUpstream(const std::string& data, int sys_errno = 0,
                               const Network::Address::Ip* local_ip = nullptr,
                               bool expect_connect = false, int connect_sys_errno = 0) {
      EXPECT_CALL(*idle_timer_, enableTimer(parent_.config_->sessionTimeout(), nullptr));
      if (expect_connect) {
        EXPECT_CALL(*socket_->io_handle_, connect(_))
            .WillOnce(Invoke([connect_sys_errno]() -> Api::SysCallIntResult {
              if (!connect_sys_errno) {
                return Api::SysCallIntResult{0, 0};
              } else {
                return Api::SysCallIntResult{-1, connect_sys_errno};
              }
            }));
      } else {
        EXPECT_CALL(*socket_->io_handle_, connect(_)).Times(0);
      }
      if (!connect_sys_errno) {
        EXPECT_CALL(*socket_->io_handle_, sendmsg(_, 1, 0, _, _))
            .WillOnce(Invoke(
                [this, data, local_ip, sys_errno](
                    const Buffer::RawSlice* slices, uint64_t, int,
                    const Network::Address::Ip* self_ip,
                    const Network::Address::Instance& peer_address) -> Api::IoCallUint64Result {
                  EXPECT_EQ(data, absl::string_view(static_cast<const char*>(slices[0].mem_),
                                                    slices[0].len_));
                  EXPECT_EQ(peer_address, *upstream_address_);
                  if (local_ip == nullptr) {
                    EXPECT_EQ(nullptr, self_ip);
                  } else {
                    EXPECT_EQ(self_ip->addressAsString(), local_ip->addressAsString());
                  }
                  // For suppression of clang-tidy NewDeleteLeaks rule, don't use the ternary
                  // operator.
                  if (sys_errno == 0) {
                    return makeNoError(data.size());
                  } else {
                    return makeError(sys_errno);
                  }
                }));
      }
    }

    void recvDataFromUpstream(const std::string& data, int recv_sys_errno = 0,
                              int send_sys_errno = 0) {
      EXPECT_CALL(*idle_timer_, enableTimer(parent_.config_->sessionTimeout(), nullptr));

      if (parent_.expect_gro_) {
        EXPECT_CALL(*socket_->io_handle_, supportsUdpGro());
      }
      EXPECT_CALL(*socket_->io_handle_, supportsMmsg()).Times(2u);
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
    NiceMock<Network::MockSocket>* socket_;
    std::map<int, std::map<int, int>> sock_opts_;
    Event::FileReadyCb file_event_cb_;
  };

  UdpProxyFilterTest()
      : UdpProxyFilterTest(Network::Utility::parseInternetAddressAndPort(peer_ip_address_)) {}

  explicit UdpProxyFilterTest(Network::Address::InstanceConstSharedPtr&& peer_address)
      : os_calls_(&os_sys_calls_),
        upstream_address_(Network::Utility::parseInternetAddressAndPort(upstream_ip_address_)),
        peer_address_(std::move(peer_address)) {
    // Disable strict mock warnings.
    ON_CALL(*factory_context_.access_log_manager_.file_, write(_))
        .WillByDefault(
            Invoke([&](absl::string_view data) { output_.push_back(std::string(data)); }));
    ON_CALL(os_sys_calls_, supportsIpTransparent()).WillByDefault(Return(true));
    EXPECT_CALL(os_sys_calls_, supportsUdpGro()).Times(AtLeast(0)).WillRepeatedly(Return(true));
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(upstream_address_));
    EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_, health())
        .WillRepeatedly(Return(Upstream::Host::Health::Healthy));
  }

  ~UdpProxyFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
             bool has_cluster = true, bool expect_gro = true) {
    config_ = std::make_shared<UdpProxyFilterConfig>(factory_context_, config);
    EXPECT_CALL(factory_context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (has_cluster) {
      factory_context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
    }
    EXPECT_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("fake_cluster"));
    filter_ = std::make_unique<TestUdpProxyFilter>(callbacks_, config_);
    expect_gro_ = expect_gro;
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
    EXPECT_CALL(
        *new_session.socket_->io_handle_,
        createFileEvent_(_, _, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read))
        .WillOnce(SaveArg<1>(&new_session.file_event_cb_));
    // Internal Buffer is Empty, flush will be a no-op
    ON_CALL(callbacks_.udp_listener_, flush())
        .WillByDefault(
            InvokeWithoutArgs([]() -> Api::IoCallUint64Result { return makeNoError(0); }));
  }

  std::shared_ptr<NiceMock<Upstream::MockHost>>
  createHost(const Network::Address::InstanceConstSharedPtr& host_address) {
    auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
    ON_CALL(*host, address()).WillByDefault(Return(host_address));
    ON_CALL(*host, health()).WillByDefault(Return(Upstream::Host::Health::Healthy));
    return host;
  }

  void checkTransferStats(uint64_t rx_bytes, uint64_t rx_datagrams, uint64_t tx_bytes,
                          uint64_t tx_datagrams) {
    EXPECT_EQ(rx_bytes, config_->stats().downstream_sess_rx_bytes_.value());
    EXPECT_EQ(rx_datagrams, config_->stats().downstream_sess_rx_datagrams_.value());
    EXPECT_EQ(tx_bytes, config_->stats().downstream_sess_tx_bytes_.value());
    EXPECT_EQ(tx_datagrams, config_->stats().downstream_sess_tx_datagrams_.value());
  }

  void checkSocketOptions(TestSession& session, const Network::SocketOptionName& ipv4_option,
                          int ipv4_expect, const Network::SocketOptionName& ipv6_option,
                          int ipv6_expect) {
    EXPECT_EQ(ipv4_expect, session.sock_opts_[ipv4_option.level()][ipv4_option.option()]);
    EXPECT_EQ(ipv6_expect, session.sock_opts_[ipv6_option.level()][ipv6_option.option()]);
  }

  void
  ensureIpTransparentSocketOptions(const Network::Address::InstanceConstSharedPtr& upstream_address,
                                   const std::string& local_address, int ipv4_expect,
                                   int ipv6_expect) {
    setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_original_src_ip: true
    )EOF"));

    expectSessionCreate(upstream_address);
    test_sessions_[0].expectSetIpTransparentSocketOption();
    test_sessions_[0].expectWriteToUpstream("hello", 0, peer_address_->ip());
    recvDataFromDownstream(peer_address_->asString(), local_address, "hello");

    checkSocketOptions(test_sessions_[0], ENVOY_SOCKET_IP_TRANSPARENT, ipv4_expect,
                       ENVOY_SOCKET_IPV6_TRANSPARENT, ipv6_expect);
    EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
    EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
    checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

    test_sessions_[0].recvDataFromUpstream("world");
    checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
  }

  // Return the config from yaml, plus one file access log with the specified format
  envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig
  accessLogConfig(const std::string& yaml, const std::string& session_access_log_format,
                  const std::string& proxy_access_log_format) {
    auto config = readConfig(yaml);

    if (!session_access_log_format.empty()) {
      envoy::config::accesslog::v3::AccessLog* session_access_log =
          config.mutable_session_access_log()->Add();
      session_access_log->set_name("envoy.access_loggers.file");
      envoy::extensions::access_loggers::file::v3::FileAccessLog session_file_access_log;
      session_file_access_log.set_path("unused");
      session_file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
          session_access_log_format);
      session_access_log->mutable_typed_config()->PackFrom(session_file_access_log);
    }

    if (!proxy_access_log_format.empty()) {
      envoy::config::accesslog::v3::AccessLog* proxy_access_log =
          config.mutable_proxy_access_log()->Add();
      proxy_access_log->set_name("envoy.access_loggers.file");
      envoy::extensions::access_loggers::file::v3::FileAccessLog proxy_file_access_log;
      proxy_file_access_log.set_path("unused");
      proxy_file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
          proxy_access_log_format);
      proxy_access_log->mutable_typed_config()->PackFrom(proxy_file_access_log);
    }
    return config;
  }

  envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig
  readConfig(const std::string& yaml) {
    envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    return config;
  }

  bool isTransparentSocketOptionsSupported() {
    for (const auto& option_name : transparent_options_) {
      if (!option_name.hasValue()) {
        return false;
      }
    }

    return true;
  }

  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_;
  NiceMock<Server::Configuration::MockListenerFactoryContext> factory_context_;
  UdpProxyFilterConfigSharedPtr config_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks_{};
  std::unique_ptr<TestUdpProxyFilter> filter_;
  std::vector<TestSession> test_sessions_;
  StringViewSaver access_log_data_;
  std::vector<std::string> output_;
  bool expect_gro_{};
  const Network::Address::InstanceConstSharedPtr upstream_address_;
  const Network::Address::InstanceConstSharedPtr peer_address_;
  const std::vector<Network::SocketOptionName> transparent_options_{ENVOY_SOCKET_IP_TRANSPARENT,
                                                                    ENVOY_SOCKET_IPV6_TRANSPARENT};
  inline static const std::string upstream_ip_address_ = "20.0.0.1:443";
  inline static const std::string peer_ip_address_ = "10.0.0.1:1000";
};

class UdpProxyFilterIpv6Test : public UdpProxyFilterTest {
public:
  UdpProxyFilterIpv6Test()
      : UdpProxyFilterIpv6Test(
            Network::Utility::parseInternetAddressAndPort(upstream_ipv6_address_)) {}

  explicit UdpProxyFilterIpv6Test(Network::Address::InstanceConstSharedPtr&& upstream_address_v6)
      : UdpProxyFilterTest(Network::Utility::parseInternetAddressAndPort(peer_ipv6_address_)),
        upstream_address_v6_(std::move(upstream_address_v6)) {
    EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_, address())
        .WillRepeatedly(Return(upstream_address_v6_));
  }

  const Network::Address::InstanceConstSharedPtr upstream_address_v6_;
  inline static const std::string upstream_ipv6_address_ = "[2001:db8:85a3::8a2e:370:7334]:443";
  inline static const std::string peer_ipv6_address_ = "[2001:db8:85a3::9a2e:370:7334]:1000";
};

class UdpProxyFilterIpv4Ipv6Test : public UdpProxyFilterIpv6Test {
public:
  UdpProxyFilterIpv4Ipv6Test()
      : UdpProxyFilterIpv6Test(Network::Utility::parseInternetAddressAndPort(
            UdpProxyFilterIpv6Test::upstream_ipv6_address_, false)) {}

  void ensureNoIpTransparentSocketOptions() {
    expectSessionCreate(upstream_address_v6_);
    test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
    recvDataFromDownstream("[2001:db8:85a3::9a2e:370:7334]:1000",
                           "[2001:db8:85a3::9a2e:370:7335]:80", "hello");

    checkSocketOptions(test_sessions_[0], ENVOY_SOCKET_IP_TRANSPARENT, 0,
                       ENVOY_SOCKET_IPV6_TRANSPARENT, 0);
    EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
    EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
    checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);

    test_sessions_[0].recvDataFromUpstream("world");
    checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
  }
};

// Basic UDP proxy flow with a single session. Also test disabling GRO.
TEST_F(UdpProxyFilterTest, BasicFlow) {
  InSequence s;

  const std::string session_access_log_format =
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)%";

  const std::string proxy_access_log_format =
      "%DYNAMIC_METADATA(udp.proxy.proxy:bytes_received)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:datagrams_received)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:bytes_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:datagrams_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:no_route)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:session_total)% "
      "%DYNAMIC_METADATA(udp.proxy.proxy:idle_timeout)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
upstream_socket_config:
  prefer_gro: false
  )EOF",
                        session_access_log_format, proxy_access_log_format),
        true, false);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  test_sessions_[0].expectWriteToUpstream("hello2");
  test_sessions_[0].expectWriteToUpstream("hello3");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  checkTransferStats(11 /*rx_bytes*/, 2 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello3");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  test_sessions_[0].recvDataFromUpstream("world2");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 11 /*tx_bytes*/, 2 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world3");
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 17 /*tx_bytes*/, 3 /*tx_datagrams*/);

  filter_.reset();
  EXPECT_EQ(output_.size(), 2);
  EXPECT_EQ(output_.front(), "17 3 17 3 0 1 0");
  EXPECT_EQ(output_.back(), "17 3 17 3");
}

// Route with source IP.
TEST_F(UdpProxyFilterTest, Router) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.source_ip
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput
    exact_match_map:
      map:
        "10.0.0.1":
          action:
            name: route
            typed_config:
              '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: fake_cluster
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  recvDataFromDownstream("10.0.0.3:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Idle timeout flow.
TEST_F(UdpProxyFilterTest, IdleTimeout) {
  InSequence s;

  const std::string session_access_log_format = "";

  const std::string proxy_access_log_format = "%DYNAMIC_METADATA(udp.proxy.proxy:session_total)% "
                                              "%DYNAMIC_METADATA(udp.proxy.proxy:idle_timeout)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF",
                        session_access_log_format, proxy_access_log_format));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  filter_.reset();
  EXPECT_EQ(output_.size(), 1);
  EXPECT_EQ(output_.front(), "2 1");
}

// Verify downstream send and receive error handling.
TEST_F(UdpProxyFilterTest, SendReceiveErrorHandling) {
  InSequence s;

  const std::string session_access_log_format =
      "%DYNAMIC_METADATA(udp.proxy.session:cluster_name)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:errors_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_received)%";

  const std::string proxy_access_log_format = "%DYNAMIC_METADATA(udp.proxy.proxy:errors_received)% "
                                              "%DYNAMIC_METADATA(udp.proxy.proxy:no_route)% "
                                              "%DYNAMIC_METADATA(udp.proxy.proxy:session_total)% "
                                              "%DYNAMIC_METADATA(udp.proxy.proxy:idle_timeout)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF",
                        session_access_log_format, proxy_access_log_format));

  filter_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
  EXPECT_EQ(1, config_->stats().downstream_sess_rx_errors_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(5, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_tx_bytes_total_.value());

  test_sessions_[0].recvDataFromUpstream("world2", 0, SOCKET_ERROR_MSG_SIZE);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_tx_errors_.value());

  test_sessions_[0].recvDataFromUpstream("world2", SOCKET_ERROR_MSG_SIZE, 0);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(
      1, TestUtility::findCounter(
             factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
             "udp.sess_rx_errors")
             ->value());

  test_sessions_[0].expectWriteToUpstream("hello", SOCKET_ERROR_MSG_SIZE);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  checkTransferStats(10 /*rx_bytes*/, 2 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(5, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_tx_bytes_total_.value());
  EXPECT_EQ(
      1, TestUtility::findCounter(
             factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
             "udp.sess_tx_errors")
             ->value());

  filter_.reset();
  EXPECT_EQ(output_.size(), 2);
  EXPECT_EQ(output_.front(), "1 0 1 0");
  EXPECT_EQ(output_.back(), "fake_cluster 0 10 1 0 2");
}

// Verify upstream connect error handling.
TEST_F(UdpProxyFilterTest, ConnectErrorHandling) {
  InSequence s;

  const std::string session_access_log_format =
      "%DYNAMIC_METADATA(udp.proxy.session:cluster_name)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:errors_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_received)%";

  const std::string proxy_access_log_format = "%DYNAMIC_METADATA(udp.proxy.proxy:errors_received)% "
                                              "%DYNAMIC_METADATA(udp.proxy.proxy:session_total)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF",
                        session_access_log_format, proxy_access_log_format));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true, 1);
  EXPECT_LOG_CONTAINS("debug", "cannot connect",
                      recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello"));
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(
      1, TestUtility::findCounter(
             factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
             "udp.sess_tx_errors")
             ->value());

  filter_.reset();
  EXPECT_EQ(output_.size(), 2);
  EXPECT_EQ(output_.front(), "0 1");
  EXPECT_EQ(output_.back(), "fake_cluster 0 5 0 0 1");
}

// No upstream host handling.
TEST_F(UdpProxyFilterTest, NoUpstreamHost) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_none_healthy_.value());
}

// No cluster at filter creation.
TEST_F(UdpProxyFilterTest, NoUpstreamClusterAtCreation) {
  InSequence s;

  const std::string session_access_log_format = "";

  const std::string proxy_access_log_format = "%DYNAMIC_METADATA(udp.proxy.proxy:no_route)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF",
                        session_access_log_format, proxy_access_log_format),
        false);

  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_no_route_.value());

  filter_.reset();
  EXPECT_EQ(output_.size(), 1);
  EXPECT_EQ(output_.front(), "1");
}

// Dynamic cluster addition and removal handling.
TEST_F(UdpProxyFilterTest, ClusterDynamicAddAndRemoval) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"),
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
  cluster_update_callbacks_->onClusterAddOrUpdate(
      factory_context_.cluster_manager_.thread_local_cluster_);
  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
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

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  // Allow only a single session.
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(
      1, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // This should hit the session circuit breaker.
  recvDataFromDownstream("10.0.0.2:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_overflow_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // Timing out the 1st session should allow us to create another.
  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.2:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Verify that all sessions for a host are removed when a host is removed.
TEST_F(UdpProxyFilterTest, RemoveHostSessions) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.priority_set_.runUpdateCallbacks(
      0, {}, {factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_});
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// In this case the host becomes unhealthy, but we get the same host back, so just keep using the
// current session.
TEST_F(UdpProxyFilterTest, HostUnhealthyPickSameHost) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_, health())
      .WillRepeatedly(Return(Upstream::Host::Health::Unhealthy));
  test_sessions_[0].expectWriteToUpstream("hello");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
}

// Make sure that we are able to create a new session if there is an available healthy host and
// our current host is unhealthy.
TEST_F(UdpProxyFilterTest, HostUnhealthyPickDifferentHost) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_, health())
      .WillRepeatedly(Return(Upstream::Host::Health::Unhealthy));
  auto new_host_address = Network::Utility::parseInternetAddressAndPort("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(new_host));
  expectSessionCreate(new_host_address);
  test_sessions_[1].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Make sure socket option is set correctly if use_original_src_ip is set.
TEST_F(UdpProxyFilterTest, SocketOptionForUseOriginalSrcIp) {
  if (!isTransparentSocketOptionsSupported()) {
    // The option is not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent());

  InSequence s;

  ensureIpTransparentSocketOptions(upstream_address_, "10.0.0.2:80", 1, 0);
}

// Verify that on second data packet sent from the client, another upstream host is selected.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingBasicFlow) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  // Allow for two sessions.
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(
      2, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  auto new_host_address = Network::Utility::parseInternetAddressAndPort("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(new_host));
  expectSessionCreate(new_host_address);
  test_sessions_[1].expectWriteToUpstream("hello2", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());
  checkTransferStats(11 /*rx_bytes*/, 2 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  // On next datagram, first session should be used
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillRepeatedly(DoDefault());
  test_sessions_[0].expectWriteToUpstream("hello3");
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello3");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());
  checkTransferStats(17 /*rx_bytes*/, 3 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);
}

// Verify that when no host is available, message is dropped.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingFirstInvalidHost) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(0, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
  EXPECT_EQ(1, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_none_healthy_.value());
}

// Verify that when on second packet no host is available, message is dropped.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingSecondInvalidHost) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  EXPECT_EQ(0, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_none_healthy_.value());

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  EXPECT_EQ(1, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_none_healthy_.value());
}

// Verify that all sessions for a host are removed when a host is removed.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingRemoveHostSessions) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.priority_set_.runUpdateCallbacks(
      0, {}, {factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_});
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello2", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
}

// Verify that all sessions for hosts in cluster are removed when a cluster is removed.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingRemoveCluster) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  // Allow for two sessions.
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(
      2, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  auto new_host_address = Network::Utility::parseInternetAddressAndPort("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Return(new_host));
  expectSessionCreate(new_host_address);
  test_sessions_[1].expectWriteToUpstream("hello2", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());

  // Remove a cluster we don't care about.
  cluster_update_callbacks_->onClusterRemoval("other_cluster");
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());

  // Remove the cluster we do care about. This should purge all sessions.
  cluster_update_callbacks_->onClusterRemoval("fake_cluster");
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
}

// Verify that specific stat is included when connection limit is hit.
TEST_F(UdpProxyFilterTest, PerPacketLoadBalancingCannotCreateConnection) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
  )EOF"));

  // Don't allow for any session.
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(
      0, 0, 0, 0, 0);

  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_
                   .upstream_cx_overflow_.value());
}

// Make sure socket option is set correctly if use_original_src_ip is set in case of ipv6.
TEST_F(UdpProxyFilterIpv6Test, SocketOptionForUseOriginalSrcIpInCaseOfIpv6) {
  if (!isTransparentSocketOptionsSupported()) {
    // The option is not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent());

  InSequence s;

  ensureIpTransparentSocketOptions(upstream_address_v6_, "[2001:db8:85a3::9a2e:370:7335]:80", 0, 1);
}

// Make sure socket options should not be set if use_original_src_ip is not set.
TEST_F(UdpProxyFilterIpv4Ipv6Test, NoSocketOptionIfUseOriginalSrcIpIsNotSet) {
  if (!isTransparentSocketOptionsSupported()) {
    // The option is not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_original_src_ip: false
  )EOF"));

  ensureNoIpTransparentSocketOptions();
}

// Make sure socket options should not be set if use_original_src_ip is not mentioned.
TEST_F(UdpProxyFilterIpv4Ipv6Test, NoSocketOptionIfUseOriginalSrcIpIsNotMentioned) {
  if (!isTransparentSocketOptionsSupported()) {
    // The option is not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }

  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  ensureNoIpTransparentSocketOptions();
}

// Make sure exit when use the use_original_src_ip but platform does not support ip
// transparent option.
TEST_F(UdpProxyFilterTest, ExitIpTransparentNoPlatformSupport) {
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent()).WillOnce(Return(false));

  InSequence s;

  auto config = R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_original_src_ip: true
  )EOF";

  EXPECT_THROW_WITH_REGEX(
      setup(readConfig(config)), EnvoyException,
      "The platform does not support either IP_TRANSPARENT or IPV6_TRANSPARENT. Or the envoy is "
      "not running with the CAP_NET_ADMIN capability.");
}

// Make sure hash policy with source_ip is created.
TEST_F(UdpProxyFilterTest, HashPolicyWithSourceIp) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- source_ip: true
  )EOF"));

  EXPECT_NE(nullptr, config_->hashPolicy());
}

// Make sure validation fails if source_ip is false.
TEST_F(UdpProxyFilterTest, ValidateHashPolicyWithSourceIp) {
  InSequence s;
  auto config = R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- source_ip: false
  )EOF";

  EXPECT_THROW_WITH_REGEX(setup(readConfig(config)), EnvoyException,
                          "caused by HashPolicyValidationError\\.SourceIp");
}

// Make sure hash policy is null if it is not mentioned.
TEST_F(UdpProxyFilterTest, NoHashPolicy) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  EXPECT_EQ(nullptr, config_->hashPolicy());
}

// Expect correct hash is created if hash_policy with source_ip is mentioned.
TEST_F(UdpProxyFilterTest, HashWithSourceIp) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- source_ip: true
  )EOF"));

  auto host = createHost(upstream_address_);
  auto generated_hash = HashUtil::xxHash64("10.0.0.1");
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([host, generated_hash](
                           Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        auto hash = context->computeHashKey();
        EXPECT_TRUE(hash.has_value());
        EXPECT_EQ(generated_hash, hash.value());
        return host;
      }));
  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  test_sessions_[0].recvDataFromUpstream("world");
}

// Expect null hash value if hash_policy is not mentioned.
TEST_F(UdpProxyFilterTest, NullHashWithoutHashPolicy) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
  )EOF"));

  auto host = createHost(upstream_address_);
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(
          Invoke([host](Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
            auto hash = context->computeHashKey();
            EXPECT_FALSE(hash.has_value());
            return host;
          }));
  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  test_sessions_[0].recvDataFromUpstream("world");
}

// Make sure hash policy with key is created.
TEST_F(UdpProxyFilterTest, HashPolicyWithKey) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- key: "key"
  )EOF"));

  EXPECT_NE(nullptr, config_->hashPolicy());
}

// Make sure validation fails if key is an empty string.
TEST_F(UdpProxyFilterTest, ValidateHashPolicyWithKey) {
  InSequence s;
  auto config = R"EOF(
stat_prefix: foo
matcher:
 on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- key: ""
  )EOF";

  EXPECT_THROW_WITH_REGEX(setup(readConfig(config)), EnvoyException,
                          "caused by HashPolicyValidationError\\.Key");
}

// Expect correct hash is created if hash_policy with key is mentioned.
TEST_F(UdpProxyFilterTest, HashWithKey) {
  InSequence s;

  setup(readConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
hash_policies:
- key: "key"
  )EOF"));

  auto host = createHost(upstream_address_);
  auto generated_hash = HashUtil::xxHash64("key");
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(Invoke([host, generated_hash](
                           Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
        auto hash = context->computeHashKey();
        EXPECT_TRUE(hash.has_value());
        EXPECT_EQ(generated_hash, hash.value());
        return host;
      }));
  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  test_sessions_[0].recvDataFromUpstream("world");
}

} // namespace
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
