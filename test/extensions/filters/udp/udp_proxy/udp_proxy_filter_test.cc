#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/hash.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/extensions/filters/udp/udp_proxy/config.h"
#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.h"
#include "test/extensions/filters/udp/udp_proxy/session_filters/drainer_filter.pb.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/socket.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer_context.h"
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
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace {

class TestUdpProxyFilter : public UdpProxyFilter {
public:
  using UdpProxyFilter::UdpProxyFilter;

  MOCK_METHOD(Network::SocketPtr, createUdpSocket, (const Upstream::HostConstSharedPtr& host));
};

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.return_value_ = rc;
  return no_error;
}

Api::IoCallUint64Result makeError(int sys_errno) {
  return {0, Network::IoSocketError::create(sys_errno)};
}

class UdpProxyFilterBase : public testing::Test {
public:
  UdpProxyFilterBase() {
    EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
        .WillRepeatedly(Invoke([&](const char* node, const char* service,
                                   const struct addrinfo* hints, struct addrinfo** res) {
          Api::OsSysCallsImpl real;
          return real.getaddrinfo(node, service, hints, res);
        }));
  }

protected:
  Api::MockOsSysCalls os_sys_calls_;
};

class UdpProxyFilterTest : public UdpProxyFilterBase {
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
        if (expect_connect) {
          EXPECT_CALL(*socket_->io_handle_, wasConnected()).WillOnce(Return(true));
          EXPECT_CALL(*socket_->io_handle_, writev(_, 1))
              .WillOnce(Invoke([data, sys_errno](const Buffer::RawSlice* slices,
                                                 uint64_t) -> Api::IoCallUint64Result {
                EXPECT_EQ(data, absl::string_view(static_cast<const char*>(slices[0].mem_),
                                                  slices[0].len_));
                // For suppression of clang-tidy NewDeleteLeaks rule, don't use the ternary
                // operator.
                if (sys_errno == 0) {
                  return makeNoError(data.size());
                } else {
                  return makeError(sys_errno);
                }
              }));
        } else {
          EXPECT_CALL(*socket_->io_handle_, wasConnected()).WillOnce(Return(false));
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
    }

    void recvDataFromUpstream(const std::string& data, int recv_sys_errno = 0,
                              int send_sys_errno = 0) {
      EXPECT_CALL(*idle_timer_, enableTimer(parent_.config_->sessionTimeout(), nullptr));

      if (parent_.expect_gro_) {
        EXPECT_CALL(*socket_->io_handle_, supportsUdpGro());
      }
      EXPECT_CALL(*socket_->io_handle_, supportsMmsg()).Times(1u);
      // Return the datagram.
      EXPECT_CALL(*socket_->io_handle_, recvmsg(_, 1, _, _, _))
          .WillOnce(
              Invoke([this, data, recv_sys_errno](
                         Buffer::RawSlice* slices, const uint64_t, uint32_t,
                         const Network::IoHandle::UdpSaveCmsgConfig&,
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
        EXPECT_CALL(*socket_->io_handle_, recvmsg(_, 1, _, _, _))
            .WillOnce(Return(ByMove(
                Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError()))));
      }

      // Kick off the receive.
      EXPECT_TRUE(file_event_cb_(Event::FileReadyType::Read).ok());
    }

    UdpProxyFilterTest& parent_;
    const Network::Address::InstanceConstSharedPtr upstream_address_;
    Event::MockTimer* idle_timer_{};
    NiceMock<Network::MockSocket>* socket_;
    std::map<int, std::map<int, int>> sock_opts_;
    Event::FileReadyCb file_event_cb_;
  };

  UdpProxyFilterTest()
      : UdpProxyFilterTest(Network::Utility::parseInternetAddressAndPortNoThrow(peer_ip_address_)) {
  }

  explicit UdpProxyFilterTest(Network::Address::InstanceConstSharedPtr&& peer_address)
      : os_calls_(&os_sys_calls_),
        upstream_address_(
            Network::Utility::parseInternetAddressAndPortNoThrow(upstream_ip_address_)),
        peer_address_(std::move(peer_address)) {
    // Disable strict mock warnings.
    ON_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_, write(_))
        .WillByDefault(
            Invoke([&](absl::string_view data) { output_.push_back(std::string(data)); }));
    ON_CALL(os_sys_calls_, supportsIpTransparent(_)).WillByDefault(Return(true));
    EXPECT_CALL(os_sys_calls_, supportsUdpGro()).Times(AtLeast(0)).WillRepeatedly(Return(true));
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(
        *factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_,
        address())
        .WillRepeatedly(Return(upstream_address_));
    EXPECT_CALL(
        *factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_,
        coarseHealth())
        .WillRepeatedly(Return(Upstream::Host::Health::Healthy));
  }

  ~UdpProxyFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config,
             bool has_cluster = true, bool expect_gro = true) {
    config_ = std::make_shared<UdpProxyFilterConfigImpl>(factory_context_, config);
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
                addThreadLocalClusterUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks_),
                        ReturnNew<Upstream::MockClusterUpdateCallbacksHandle>()));
    if (has_cluster) {
      factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
          {"fake_cluster"});
    }
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
                getThreadLocalCluster("fake_cluster"));
    filter_ = std::make_unique<TestUdpProxyFilter>(callbacks_, config_);
    expect_gro_ = expect_gro;
  }

  void recvDataFromDownstream(const std::string& peer_address, const std::string& local_address,
                              const std::string& buffer) {
    Network::UdpRecvData data;
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow(peer_address);
    data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow(local_address);
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  void expectSessionCreate(const Network::Address::InstanceConstSharedPtr& address) {
    test_sessions_.emplace_back(*this, address);
    TestSession& new_session = test_sessions_.back();
    new_session.idle_timer_ = new Event::MockTimer(&callbacks_.udp_listener_.dispatcher_);
    EXPECT_CALL(*filter_, createUdpSocket(_))
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
    ON_CALL(*host, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
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
          config.mutable_access_log()->Add();
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
            Network::Utility::parseInternetAddressAndPortNoThrow(upstream_ipv6_address_)) {}

  explicit UdpProxyFilterIpv6Test(Network::Address::InstanceConstSharedPtr&& upstream_address_v6)
      : UdpProxyFilterTest(
            Network::Utility::parseInternetAddressAndPortNoThrow(peer_ipv6_address_)),
        upstream_address_v6_(std::move(upstream_address_v6)) {
    EXPECT_CALL(
        *factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_,
        address())
        .WillRepeatedly(Return(upstream_address_v6_));
  }

  const Network::Address::InstanceConstSharedPtr upstream_address_v6_;
  inline static const std::string upstream_ipv6_address_ = "[2001:db8:85a3::8a2e:370:7334]:443";
  inline static const std::string peer_ipv6_address_ = "[2001:db8:85a3::9a2e:370:7334]:1000";
};

class UdpProxyFilterIpv4Ipv6Test : public UdpProxyFilterIpv6Test {
public:
  UdpProxyFilterIpv4Ipv6Test()
      : UdpProxyFilterIpv6Test(Network::Utility::parseInternetAddressAndPortNoThrow(
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

// Basic UDP proxy flow with two sessions. Also test disabling GRO.
TEST_F(UdpProxyFilterTest, BasicFlow) {
  InSequence s;

  const std::string session_access_log_format =
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_received)% "
      "%DYNAMIC_METADATA(udp.proxy.session:bytes_sent)% "
      "%DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)% "
      "%CONNECTION_ID% "
      "%DOWNSTREAM_REMOTE_ADDRESS% "
      "%DOWNSTREAM_LOCAL_ADDRESS% "
      "%UPSTREAM_HOST% "
      "%STREAM_ID% "
      "%ACCESS_LOG_TYPE%";

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

  // Allow for two sessions.
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(2, 0, 0, 0, 0);

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

  expectSessionCreate(upstream_address_);
  test_sessions_[1].expectWriteToUpstream("hello4", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.3:1000", "10.0.0.2:80", "hello4");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());
  checkTransferStats(23 /*rx_bytes*/, 4 /*rx_datagrams*/, 17 /*tx_bytes*/, 3 /*tx_datagrams*/);
  test_sessions_[1].recvDataFromUpstream("world4");
  checkTransferStats(23 /*rx_bytes*/, 4 /*rx_datagrams*/, 23 /*tx_bytes*/, 4 /*tx_datagrams*/);

  filter_.reset();
  EXPECT_EQ(output_.size(), 3);
  EXPECT_EQ(output_[0], "23 4 23 4 0 2 0");

  const std::string session_access_log_regex =
      "(17 3 17 3 0|6 1 6 1 1) 10.0.0.(1|3):1000 10.0.0.2:80 20.0.0.1:443 "
      "[0-9a-fA-F]{8}-([0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12} " +
      AccessLogType_Name(AccessLog::AccessLogType::UdpSessionEnd);

  EXPECT_TRUE(std::regex_match(output_[1], std::regex(session_access_log_regex)));
  EXPECT_TRUE(std::regex_match(output_[2], std::regex(session_access_log_regex)));
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
  EXPECT_EQ(5, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_tx_bytes_total_.value());

  test_sessions_[0].recvDataFromUpstream("world2", 0, SOCKET_ERROR_MSG_SIZE);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_tx_errors_.value());

  test_sessions_[0].recvDataFromUpstream("world2", SOCKET_ERROR_MSG_SIZE, 0);
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(6, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_rx_bytes_total_.value());
  EXPECT_EQ(1, TestUtility::findCounter(factory_context_.server_factory_context_.cluster_manager_
                                            .thread_local_cluster_.cluster_.info_->stats_store_,
                                        "udp.sess_rx_errors")
                   ->value());

  test_sessions_[0].expectWriteToUpstream("hello", SOCKET_ERROR_MSG_SIZE);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  checkTransferStats(10 /*rx_bytes*/, 2 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  EXPECT_EQ(5, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_tx_bytes_total_.value());
  EXPECT_EQ(1, TestUtility::findCounter(factory_context_.server_factory_context_.cluster_manager_
                                            .thread_local_cluster_.cluster_.info_->stats_store_,
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
  EXPECT_EQ(1, TestUtility::findCounter(factory_context_.server_factory_context_.cluster_manager_
                                            .thread_local_cluster_.cluster_.info_->stats_store_,
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

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_none_healthy_.value());
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
  {
    Upstream::ThreadLocalClusterCommand command =
        [&other_thread_local_cluster]() -> Upstream::ThreadLocalCluster& {
      return other_thread_local_cluster;
    };
    cluster_update_callbacks_->onClusterAddOrUpdate(other_thread_local_cluster.info()->name(),
                                                    command);
  }
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(2, config_->stats().downstream_sess_no_route_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  // Now add the cluster we care about.
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_;
    };
    cluster_update_callbacks_->onClusterAddOrUpdate(
        factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.info()
            ->name(),
        command);
  }
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

// Test updates to existing cluster (e.g. priority set changes, etc).
TEST_F(UdpProxyFilterTest, ClusterDynamicInfoMapUpdate) {
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

  // Initial ThreadLocalCluster, scoped lifetime
  // mimics replacement of old ThreadLocalCluster via postThreadLocalClusterUpdate.
  {
    NiceMock<Upstream::MockThreadLocalCluster> other_thread_local_cluster;
    other_thread_local_cluster.cluster_.info_->name_ = "fake_cluster";
    Upstream::ThreadLocalClusterCommand command =
        [&other_thread_local_cluster]() -> Upstream::ThreadLocalCluster& {
      return other_thread_local_cluster;
    };
    cluster_update_callbacks_->onClusterAddOrUpdate(other_thread_local_cluster.info()->name(),
                                                    command);
  }

  // Push new cluster (getter), we expect this to result in new ClusterInfos object in map.
  {
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_;
    };
    cluster_update_callbacks_->onClusterAddOrUpdate(
        factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.info()
            ->name(),
        command);
  }

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
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
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(1, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  // This should hit the session circuit breaker.
  recvDataFromDownstream("10.0.0.2:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_overflow_.value());
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

  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_
      .priority_set_.runUpdateCallbacks(0, {},
                                        {factory_context_.server_factory_context_.cluster_manager_
                                             .thread_local_cluster_.lb_.host_});
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

  EXPECT_CALL(
      *factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_,
      coarseHealth())
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

  EXPECT_CALL(
      *factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_.host_,
      coarseHealth())
      .WillRepeatedly(Return(Upstream::Host::Health::Unhealthy));
  auto new_host_address = Network::Utility::parseInternetAddressAndPortNoThrow("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent(_));

  InSequence s;

  ensureIpTransparentSocketOptions(upstream_address_, "10.0.0.2:80", 1, 0);
}

TEST_F(UdpProxyFilterTest, MutualExcludePerPacketLoadBalancingAndSessionFilters) {
  auto config = R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
session_filters:
- name: foo
  typed_config:
    '@type': type.googleapis.com/google.protobuf.Struct
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setup(readConfig(config)), EnvoyException,
      "Only one of use_per_packet_load_balancing or session_filters can be used.");
}

TEST_F(UdpProxyFilterTest, MutualExcludePerPacketLoadBalancingAndTunneling) {
  auto config = R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
use_per_packet_load_balancing: true
tunneling_config:
  proxy_host: host.com
  target_host: host.com
  default_target_port: 30
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setup(readConfig(config)), EnvoyException,
      "Only one of use_per_packet_load_balancing or tunneling_config can be used.");
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
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(2, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 0 /*tx_bytes*/, 0 /*tx_datagrams*/);
  test_sessions_[0].recvDataFromUpstream("world");
  checkTransferStats(5 /*rx_bytes*/, 1 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  auto new_host_address = Network::Utility::parseInternetAddressAndPortNoThrow("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
      .WillOnce(Return(new_host));
  expectSessionCreate(new_host_address);
  test_sessions_[1].expectWriteToUpstream("hello2", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(2, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(2, config_->stats().downstream_sess_active_.value());
  checkTransferStats(11 /*rx_bytes*/, 2 /*rx_datagrams*/, 5 /*tx_bytes*/, 1 /*tx_datagrams*/);

  // On next datagram, first session should be used
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(0, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());
  EXPECT_EQ(1, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_none_healthy_.value());
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
  EXPECT_EQ(0, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_none_healthy_.value());

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
      .WillOnce(Return(nullptr));
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello2");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());
  EXPECT_EQ(1, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_none_healthy_.value());
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

  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_
      .priority_set_.runUpdateCallbacks(0, {},
                                        {factory_context_.server_factory_context_.cluster_manager_
                                             .thread_local_cluster_.lb_.host_});
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
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(2, 0, 0, 0, 0);

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  auto new_host_address = Network::Utility::parseInternetAddressAndPortNoThrow("20.0.0.2:443");
  auto new_host = createHost(new_host_address);
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(0, 0, 0, 0, 0);

  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_->traffic_stats_->upstream_cx_overflow_.value());
}

// Make sure socket option is set correctly if use_original_src_ip is set in case of ipv6.
TEST_F(UdpProxyFilterIpv6Test, SocketOptionForUseOriginalSrcIpInCaseOfIpv6) {
  if (!isTransparentSocketOptionsSupported()) {
    // The option is not supported on this platform. Just skip the test.
    GTEST_SKIP();
  }
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent(_));

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
  EXPECT_CALL(os_sys_calls_, supportsIpTransparent(_)).WillOnce(Return(false));

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
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_,
              chooseHost(_))
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

TEST_F(UdpProxyFilterTest, EnrichAccessLogOnSessionComplete) {
  InSequence s;

  const std::string session_access_log_format =
      "%FILTER_STATE(test.udp_session.drainer.on_session_complete)%";

  setup(accessLogConfig(R"EOF(
stat_prefix: foo
matcher:
  on_no_match:
    action:
      name: route
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
        cluster: fake_cluster
session_filters:
- name: foo
  typed_config:
    '@type': type.googleapis.com/test.extensions.filters.udp.udp_proxy.session_filters.DrainerUdpSessionReadFilterConfig
    downstream_bytes_to_drain: 0
    stop_iteration_on_new_session: false
    stop_iteration_on_first_read: false
    continue_filter_chain: false
  )EOF",
                        session_access_log_format, ""));

  expectSessionCreate(upstream_address_);
  test_sessions_[0].expectWriteToUpstream("hello", 0, nullptr, true);
  recvDataFromDownstream("10.0.0.1:1000", "10.0.0.2:80", "hello");
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(1, config_->stats().downstream_sess_active_.value());

  test_sessions_[0].idle_timer_->invokeCallback();
  EXPECT_EQ(1, config_->stats().downstream_sess_total_.value());
  EXPECT_EQ(0, config_->stats().downstream_sess_active_.value());

  filter_.reset();
  EXPECT_EQ(output_.size(), 1);
  EXPECT_THAT(output_.front(), testing::HasSubstr("session_complete"));
}

using MockUdpTunnelingConfig = SessionFilters::MockUdpTunnelingConfig;
using MockUpstreamTunnelCallbacks = SessionFilters::MockUpstreamTunnelCallbacks;
using MockTunnelCreationCallbacks = SessionFilters::MockTunnelCreationCallbacks;

class HttpUpstreamImplTest : public testing::Test {
public:
  struct HeaderToAdd {
    std::string key_;
    std::string value_;
  };

  Http::TestRequestHeaderMapImpl
  expectedHeaders(bool is_ssl = false, bool use_post = false,
                  absl::optional<std::string> opt_authority = absl::nullopt,
                  absl::optional<std::string> opt_path = absl::nullopt,
                  absl::optional<HeaderToAdd> header_to_add = absl::nullopt) {
    // In case connect-udp is used, Envoy expect the H2 headers to be normalized with H1,
    // so expect that the request headers here match H1 headers, even though
    // eventually H2 headers will be sent. When the headers are normalized to H1, the method
    // is replaced with GET, a header with the 'upgrade' key is added with 'connect-udp'
    // value and a header with the 'connection' key is added with 'Upgrade' value.
    std::string scheme = is_ssl ? "https" : "http";
    std::string authority =
        opt_authority.has_value() ? opt_authority.value() : "default.host.com:10";
    std::string method = use_post ? "POST" : "GET";
    std::string path =
        opt_path.has_value() ? opt_path.value() : "/.well-known/masque/udp/default.target.host/20/";

    auto headers = Http::TestRequestHeaderMapImpl{
        {":scheme", scheme}, {":authority", authority}, {":method", method}, {":path", path}};

    if (!use_post) {
      headers.addCopy("capsule-protocol", "?1");
      headers.addCopy("upgrade", "connect-udp");
      headers.addCopy("connection", "Upgrade");
    }

    if (header_to_add) {
      headers.addCopy(header_to_add.value().key_, header_to_add.value().value_);
    }

    return headers;
  }

  void setAndExpectRequestEncoder(Http::TestRequestHeaderMapImpl expected_headers,
                                  bool is_ssl = false) {
    EXPECT_CALL(request_encoder_.stream_, addCallbacks(_));
    EXPECT_CALL(request_encoder_, encodeHeaders(_, _))
        .WillOnce(Invoke([expected_headers](const Http::RequestHeaderMap& headers,
                                            bool end_stream) -> Http::Status {
          EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
          EXPECT_FALSE(end_stream);
          return absl::OkStatus();
        }));

    upstream_->setRequestEncoder(request_encoder_, is_ssl);

    // Verify that resetEncoder is called only once.
    EXPECT_CALL(request_encoder_.stream_, removeCallbacks(_));
  }

  void filterStateOverride(absl::optional<uint32_t> proxy_port = absl::nullopt,
                           absl::optional<uint32_t> target_port = absl::nullopt) {
    if (proxy_port) {
      stream_info_.filterState()->setData(
          "udp.connect.proxy_port",
          std::make_shared<StreamInfo::UInt32AccessorImpl>(proxy_port.value()),
          Envoy::StreamInfo::FilterState::StateType::Mutable);
    }

    if (target_port) {
      stream_info_.filterState()->setData(
          "udp.connect.target_port",
          std::make_shared<StreamInfo::UInt32AccessorImpl>(target_port.value()),
          Envoy::StreamInfo::FilterState::StateType::Mutable);
    }
  }

  void setup(absl::optional<HeaderToAdd> header_to_add = absl::nullopt) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> headers_to_add;
    if (header_to_add) {
      envoy::config::core::v3::HeaderValueOption* header = headers_to_add.Add();
      header->mutable_header()->set_key(header_to_add.value().key_);
      header->mutable_header()->set_value(header_to_add.value().value_);
    }

    header_evaluator_ = Envoy::Router::HeaderParser::configure(headers_to_add).value();
    config_ = std::make_unique<NiceMock<MockUdpTunnelingConfig>>(*header_evaluator_);
    upstream_ = std::make_unique<HttpUpstreamImpl>(callbacks_, *config_, stream_info_);
    upstream_->setTunnelCreationCallbacks(creation_callbacks_);
  }

  NiceMock<MockUpstreamTunnelCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Http::MockRequestEncoder> request_encoder_;
  NiceMock<MockTunnelCreationCallbacks> creation_callbacks_;
  std::unique_ptr<NiceMock<MockUdpTunnelingConfig>> config_;
  std::unique_ptr<Http::HeaderEvaluator> header_evaluator_;
  std::unique_ptr<HttpUpstreamImpl> upstream_;
};

TEST_F(HttpUpstreamImplTest, EncodeData) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  EXPECT_CALL(request_encoder_, encodeData(_, false));
  Buffer::OwnedImpl data;
  upstream_->encodeData(data);
}

TEST_F(HttpUpstreamImplTest, WatermarksCallBack) {
  setup();

  EXPECT_CALL(callbacks_, onAboveWriteBufferHighWatermark());
  upstream_->onAboveWriteBufferHighWatermark();
  EXPECT_CALL(callbacks_, onBelowWriteBufferLowWatermark());
  upstream_->onBelowWriteBufferLowWatermark();
}

TEST_F(HttpUpstreamImplTest, OnResetStream) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  // If the creation callbacks are active, it means that response headers were not received,
  // so it's expected to call onStreamFailure.
  EXPECT_CALL(creation_callbacks_, onStreamFailure());
  upstream_->onResetStream(Http::StreamResetReason::ConnectionTimeout, "reason");
}

TEST_F(HttpUpstreamImplTest, LocalCloseByDownstreamResetsStream) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  // If the creation callbacks are active, it means that response headers were not received,
  // so it's expected to reset the stream, but not to call onStreamFailure as it's Downstream event.
  EXPECT_CALL(request_encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(creation_callbacks_, onStreamFailure()).Times(0);
  upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpUpstreamImplTest, RemoteCloseByDownstreamResetsStream) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  // If the creation callbacks are active, it means that response headers were not received,
  // so it's expected to reset the stream, but not to call onStreamFailure as it's Downstream event.
  EXPECT_CALL(request_encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(creation_callbacks_, onStreamFailure()).Times(0);
  upstream_->onDownstreamEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpUpstreamImplTest, NullOperationFunctions) {
  setup();

  // Calling functions that implemented by no-op for coverage.
  upstream_->responseDecoder().decodeMetadata(nullptr);
  upstream_->responseDecoder().decode1xxHeaders(nullptr);
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  upstream_->responseDecoder().dumpState(ostream, 1);
}

TEST_F(HttpUpstreamImplTest, FailureResponseHeadersNot200Status) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  EXPECT_CALL(creation_callbacks_, onStreamFailure());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}, {"Capsule-Protocol", "?1"}});
  upstream_->responseDecoder().decodeHeaders(std::move(response_headers), /*end_stream=*/false);
}

TEST_F(HttpUpstreamImplTest, FailureResponseHeadersEndStream) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  EXPECT_CALL(creation_callbacks_, onStreamFailure());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "101"}, {"upgrade", "connect-udp"}});
  upstream_->responseDecoder().decodeHeaders(std::move(response_headers), /*end_stream=*/true);
}

TEST_F(HttpUpstreamImplTest, SuccessResponseHeaders) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  EXPECT_CALL(creation_callbacks_, onStreamFailure()).Times(0);
  EXPECT_CALL(creation_callbacks_, onStreamSuccess(_));

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "101"}, {"upgrade", "connect-udp"}});
  upstream_->responseDecoder().decodeHeaders(std::move(response_headers), /*end_stream=*/false);
}

TEST_F(HttpUpstreamImplTest, DecodeData) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  Buffer::OwnedImpl data;

  // Second decodeData call will reset the stream, because it is end of stream.
  // Since response headers were not received it's considered as a failure.
  EXPECT_CALL(creation_callbacks_, onStreamFailure());
  EXPECT_CALL(callbacks_, onUpstreamData(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, bool end_stream) { EXPECT_FALSE(end_stream); }))
      .WillOnce(Invoke([](Buffer::Instance&, bool end_stream) { EXPECT_TRUE(end_stream); }));

  upstream_->responseDecoder().decodeData(data, /*end_stream=*/false);
  upstream_->responseDecoder().decodeData(data, /*end_stream=*/true);
}

TEST_F(HttpUpstreamImplTest, DecodeDataAfterSuccessHeaders) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  Buffer::OwnedImpl data;
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "101"}, {"upgrade", "connect-udp"}});

  EXPECT_CALL(creation_callbacks_, onStreamFailure()).Times(0);
  EXPECT_CALL(creation_callbacks_, onStreamSuccess(_));
  EXPECT_CALL(callbacks_, onUpstreamData(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, bool end_stream) { EXPECT_FALSE(end_stream); }))
      .WillOnce(Invoke([](Buffer::Instance&, bool end_stream) { EXPECT_TRUE(end_stream); }));

  upstream_->responseDecoder().decodeHeaders(std::move(response_headers), /*end_stream=*/false);
  upstream_->responseDecoder().decodeData(data, /*end_stream=*/false);
  upstream_->responseDecoder().decodeData(data, /*end_stream=*/true);
}

TEST_F(HttpUpstreamImplTest, DecodeTrailers) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  // Decode trailers should reset the stream.
  EXPECT_CALL(creation_callbacks_, onStreamFailure());
  upstream_->responseDecoder().decodeTrailers(nullptr);
}

TEST_F(HttpUpstreamImplTest, DecodeTrailersAfterSuccessHeaders) {
  setup();
  setAndExpectRequestEncoder(expectedHeaders());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "101"}, {"upgrade", "connect-udp"}});

  EXPECT_CALL(creation_callbacks_, onStreamFailure()).Times(0);
  EXPECT_CALL(creation_callbacks_, onStreamSuccess(_));

  upstream_->responseDecoder().decodeHeaders(std::move(response_headers), /*end_stream=*/false);
  upstream_->responseDecoder().decodeTrailers(nullptr);
}

TEST_F(HttpUpstreamImplTest, EncodeHeaders) {
  HeaderToAdd header{"test_key", "test_val"};
  absl::optional<uint32_t> port;
  bool is_ssl = false;

  setup(header);

  EXPECT_CALL(*config_, proxyHost(_)).WillRepeatedly(Return("proxy.host"));
  EXPECT_CALL(*config_, proxyPort()).WillRepeatedly(ReturnRef(port));
  EXPECT_CALL(*config_, targetHost(_)).WillRepeatedly(Return("target.host"));
  EXPECT_CALL(*config_, defaultTargetPort()).WillRepeatedly(Return(200));

  auto expected_headers = expectedHeaders(
      is_ssl, /*use_post=*/false, /*opt_authority=*/"proxy.host",
      /*opt_path=*/"/.well-known/masque/udp/target.host/200/", /*header_to_add=*/header);

  setAndExpectRequestEncoder(expected_headers, is_ssl);
}

TEST_F(HttpUpstreamImplTest, EncodeHeadersWithPost) {
  std::string post_path = "/post/path";
  absl::optional<uint32_t> port = 100;
  bool is_ssl = true;

  setup();

  EXPECT_CALL(*config_, proxyHost(_)).WillRepeatedly(Return("proxy.host"));
  EXPECT_CALL(*config_, proxyPort()).WillRepeatedly(ReturnRef(port));
  EXPECT_CALL(*config_, usePost()).WillRepeatedly(Return(true));
  EXPECT_CALL(*config_, postPath()).WillRepeatedly(ReturnRef(post_path));

  auto expected_headers =
      expectedHeaders(is_ssl, /*use_post=*/true, /*opt_authority=*/"proxy.host:100",
                      /*opt_path=*/post_path, /*header_to_add=*/absl::nullopt);

  setAndExpectRequestEncoder(expected_headers, is_ssl);
}

TEST_F(HttpUpstreamImplTest, EncodeHeadersWithFilterStateOverrides) {
  HeaderToAdd header{"test_key", "test_val"};
  bool is_ssl = false;

  setup(header);
  filterStateOverride(30, 60);

  EXPECT_CALL(*config_, proxyHost(_)).WillRepeatedly(Return("proxy.host"));
  EXPECT_CALL(*config_, targetHost(_)).WillRepeatedly(Return("target.host"));

  // Filter state override has precedence, so they are not called.
  EXPECT_CALL(*config_, proxyPort()).Times(0);
  EXPECT_CALL(*config_, defaultTargetPort()).Times(0);

  auto expected_headers = expectedHeaders(
      is_ssl, /*use_post=*/false, /*opt_authority=*/"proxy.host:30",
      /*opt_path=*/"/.well-known/masque/udp/target.host/60/", /*header_to_add=*/header);

  setAndExpectRequestEncoder(expected_headers, is_ssl);
}

TEST_F(HttpUpstreamImplTest, TargetHostPercentEncoding) {
  bool is_ssl = false;
  setup();

  EXPECT_CALL(*config_, proxyHost(_)).WillRepeatedly(Return("proxy.host"));
  EXPECT_CALL(*config_, targetHost(_)).WillRepeatedly(Return("2001:db8::42"));

  auto expected_headers =
      expectedHeaders(is_ssl, /*use_post=*/false, /*opt_authority=*/"proxy.host:10",
                      /*opt_path=*/"/.well-known/masque/udp/2001%3Adb8%3A%3A42/20/");

  setAndExpectRequestEncoder(expected_headers, is_ssl);
}

using MockHttpStreamCallbacks = SessionFilters::MockHttpStreamCallbacks;

class TunnelingConnectionPoolImplTest : public testing::Test {
public:
  void setup() {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> headers_to_add;
    header_evaluator_ = Envoy::Router::HeaderParser::configure(headers_to_add).value();
    config_ = std::make_unique<NiceMock<MockUdpTunnelingConfig>>(*header_evaluator_);
    stream_info_.downstream_connection_info_provider_->setConnectionID(0);
    pool_ = std::make_unique<TunnelingConnectionPoolImpl>(cluster_, &context_, *config_, callbacks_,
                                                          stream_info_);
  }

  void createNewStream() { pool_->newStream(stream_callbacks_); }

  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  NiceMock<Upstream::MockLoadBalancerContext> context_;
  std::unique_ptr<Http::HeaderEvaluator> header_evaluator_;
  std::unique_ptr<NiceMock<MockUdpTunnelingConfig>> config_;
  NiceMock<MockUpstreamTunnelCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<MockHttpStreamCallbacks> stream_callbacks_;
  NiceMock<Http::MockRequestEncoder> request_encoder_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> upstream_host_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::unique_ptr<TunnelingConnectionPoolImpl> pool_;
};

TEST_F(TunnelingConnectionPoolImplTest, ValidPool) {
  setup();
  EXPECT_TRUE(pool_->valid());
}

TEST_F(TunnelingConnectionPoolImplTest, InvalidPool) {
  EXPECT_CALL(cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  setup();
  EXPECT_FALSE(pool_->valid());
}

TEST_F(TunnelingConnectionPoolImplTest, OnNewStream) {
  setup();
  EXPECT_CALL(cluster_.conn_pool_, newStream(_, _, _));
  createNewStream();
}

TEST_F(TunnelingConnectionPoolImplTest, PoolFailure) {
  setup();
  createNewStream();
  EXPECT_CALL(stream_callbacks_, onStreamFailure(_, _, _));

  std::string upstream_host_name = "upstream_host_test";
  EXPECT_CALL(*upstream_host_, hostname()).WillOnce(ReturnRef(upstream_host_name));
  pool_->onPoolFailure(Http::ConnectionPool::PoolFailureReason::Timeout, "reason", upstream_host_);
  EXPECT_EQ(stream_info_.upstreamInfo()->upstreamHost()->hostname(), upstream_host_name);
  EXPECT_EQ(stream_info_.upstreamInfo()->upstreamTransportFailureReason(), "reason");
}

TEST_F(TunnelingConnectionPoolImplTest, PoolReady) {
  setup();
  createNewStream();
  EXPECT_CALL(request_encoder_.stream_, addCallbacks(_));

  std::string upstream_host_name = "upstream_host_test";
  EXPECT_CALL(*upstream_host_, hostname()).WillOnce(ReturnRef(upstream_host_name));
  EXPECT_CALL(stream_callbacks_, resetIdleTimer());
  pool_->onPoolReady(request_encoder_, upstream_host_, stream_info_, absl::nullopt);
  EXPECT_EQ(stream_info_.upstreamInfo()->upstreamHost()->hostname(), upstream_host_name);
}

TEST_F(TunnelingConnectionPoolImplTest, OnStreamFailure) {
  setup();
  createNewStream();
  EXPECT_CALL(stream_callbacks_,
              onStreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "", _));
  pool_->onStreamFailure();
}

TEST_F(TunnelingConnectionPoolImplTest, OnStreamSuccess) {
  setup();
  createNewStream();
  EXPECT_CALL(stream_callbacks_, onStreamReady(_, _, _, _, _));
  pool_->onStreamSuccess(request_encoder_);
}

TEST_F(TunnelingConnectionPoolImplTest, OnDownstreamEvent) {
  setup();
  createNewStream();

  EXPECT_CALL(request_encoder_.stream_, addCallbacks(_));
  pool_->onPoolReady(request_encoder_, upstream_host_, stream_info_, absl::nullopt);

  EXPECT_CALL(request_encoder_.stream_, removeCallbacks(_));
  EXPECT_CALL(request_encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  pool_->onDownstreamEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TunnelingConnectionPoolImplTest, FactoryTest) {
  setup();

  TunnelingConnectionPoolFactory factory;
  auto valid_pool = factory.createConnPool(cluster_, &context_, *config_, callbacks_, stream_info_);
  EXPECT_FALSE(valid_pool == nullptr);

  EXPECT_CALL(cluster_, httpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));
  auto invalid_pool =
      factory.createConnPool(cluster_, &context_, *config_, callbacks_, stream_info_);
  EXPECT_TRUE(invalid_pool == nullptr);
}

TEST(TunnelingConfigImplTest, DefaultConfigs) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TunnelingConfig proto_config;
  proto_config.set_use_post(true);
  TunnelingConfigImpl config(proto_config, context);

  EXPECT_EQ(1, config.maxConnectAttempts());
  EXPECT_EQ(1024, config.maxBufferedDatagrams());
  EXPECT_EQ(16384, config.maxBufferedBytes());
}

TEST(TunnelingConfigImplTest, DefaultPostPath) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TunnelingConfig proto_config;
  proto_config.set_use_post(true);
  TunnelingConfigImpl config(proto_config, context);

  EXPECT_EQ("/", config.postPath());
}

TEST(TunnelingConfigImplTest, PostPathWithoutPostMethod) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TunnelingConfig proto_config;
  proto_config.set_post_path("/path");
  EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                            "Can't set a post path when POST method isn't used");
}

TEST(TunnelingConfigImplTest, PostWithInvalidPath) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TunnelingConfig proto_config;
  proto_config.set_use_post(true);
  proto_config.set_post_path("path");
  EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                            "Path must start with '/'");
}

TEST(TunnelingConfigImplTest, ValidProxyPort) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TunnelingConfig proto_config;
  proto_config.mutable_proxy_port()->set_value(443);
  TunnelingConfigImpl config(proto_config, context);
  EXPECT_EQ(443, config.proxyPort());
}

TEST(TunnelingConfigImplTest, ProxyPortOutOfRange) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  {
    TunnelingConfig proto_config;
    proto_config.mutable_proxy_port()->set_value(0);
    EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                              "Port value not in range");
  }
  {
    TunnelingConfig proto_config;
    proto_config.mutable_proxy_port()->set_value(65536);
    EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                              "Port value not in range");
  }
}

TEST(TunnelingConfigImplTest, InvalidHeadersToAdd) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  {
    TunnelingConfig proto_config;
    auto* header_to_add = proto_config.add_headers_to_add();
    auto* header = header_to_add->mutable_header();
    // Can't add pseudo header.
    header->set_key(":method");
    header->set_value("GET");
    EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                              ":-prefixed or host headers may not be modified");
  }

  {
    TunnelingConfig proto_config;
    auto* header_to_add = proto_config.add_headers_to_add();
    auto* header = header_to_add->mutable_header();
    // Can't modify host.
    header->set_key("host");
    header->set_value("example.net:80");
    EXPECT_THROW_WITH_MESSAGE(TunnelingConfigImpl(proto_config, context), EnvoyException,
                              ":-prefixed or host headers may not be modified");
  }
}

TEST(TunnelingConfigImplTest, HeadersToAdd) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  stream_info.filterState()->setData(
      "test_key", std::make_shared<Envoy::Router::StringAccessorImpl>("test_val"),
      Envoy::StreamInfo::FilterState::StateType::Mutable);

  TunnelingConfig proto_config;
  auto* header_to_add = proto_config.add_headers_to_add();
  auto* header = header_to_add->mutable_header();
  header->set_key("test_key");
  header->set_value("%FILTER_STATE(test_key:PLAIN)%");
  TunnelingConfigImpl config(proto_config, context);

  auto headers = Http::TestRequestHeaderMapImpl{{":scheme", "http"}, {":authority", "host.com"}};
  config.headerEvaluator().evaluateHeaders(headers, {}, stream_info);
  EXPECT_EQ("test_val", headers.get_("test_key"));
}

TEST(TunnelingConfigImplTest, ProxyHostFromFilterState) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  stream_info.filterState()->setData(
      "test-proxy-host", std::make_shared<Envoy::Router::StringAccessorImpl>("test.host.com"),
      Envoy::StreamInfo::FilterState::StateType::Mutable);

  TunnelingConfig proto_config;
  proto_config.set_proxy_host("%FILTER_STATE(test-proxy-host:PLAIN)%");
  TunnelingConfigImpl config(proto_config, context);

  EXPECT_EQ("test.host.com", config.proxyHost(stream_info));
}

TEST(TunnelingConfigImplTest, TargetHostFromFilterState) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  stream_info.filterState()->setData(
      "test-proxy-host", std::make_shared<Envoy::Router::StringAccessorImpl>("test.host.com"),
      Envoy::StreamInfo::FilterState::StateType::Mutable);

  TunnelingConfig proto_config;
  proto_config.set_target_host("%FILTER_STATE(test-proxy-host:PLAIN)%");
  TunnelingConfigImpl config(proto_config, context);

  EXPECT_EQ("test.host.com", config.targetHost(stream_info));
}

TEST(TunnelingConfigImplTest, BufferingState) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  {
    TunnelingConfig proto_config;
    TunnelingConfigImpl config(proto_config, context);
    // Buffering should be disabled by default.
    EXPECT_FALSE(config.bufferEnabled());
  }

  {
    TunnelingConfig proto_config;
    proto_config.mutable_buffer_options(); // Will set buffering.
    TunnelingConfigImpl config(proto_config, context);
    EXPECT_TRUE(config.bufferEnabled());
  }
}

} // namespace
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
