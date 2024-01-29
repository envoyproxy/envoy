#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool.pb.h"
#include "envoy/extensions/upstreams/tcp/generic/v3/generic_connection_pool.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace TcpProxy {

namespace {
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::ReturnPointee;
using ::testing::SaveArg;
} // namespace

inline Config constructConfigFromYaml(const std::string& yaml,
                                      Server::Configuration::FactoryContext& context) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);
  return {tcp_proxy, context};
}

class TcpProxyTestBase : public testing::Test {
public:
  TcpProxyTestBase() {
    ON_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_, write(_))
        .WillByDefault(SaveArg<0>(&access_log_data_));
    ON_CALL(filter_callbacks_.connection_.stream_info_, setUpstreamClusterInfo(_))
        .WillByDefault(Invoke([this](const Upstream::ClusterInfoConstSharedPtr& cluster_info) {
          upstream_cluster_ = cluster_info;
        }));
    ON_CALL(filter_callbacks_.connection_.stream_info_, upstreamClusterInfo())
        .WillByDefault(ReturnPointee(&upstream_cluster_));
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
  }

  ~TcpProxyTestBase() override {
    if (filter_ != nullptr) {
      filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
    }
  }

  void configure(const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) {
    config_ = std::make_shared<Config>(config, factory_context_);
  }

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy defaultConfig() {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
    config.set_stat_prefix("name");
    config.set_cluster("fake_cluster");
    return config;
  }

  // Return the default config, plus one file access log with the specified format
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy
  accessLogConfig(const std::string& access_log_format) {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
    envoy::config::accesslog::v3::AccessLog* access_log = config.mutable_access_log()->Add();
    access_log->set_name("envoy.access_loggers.file");
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("unused");
    file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        access_log_format);
    access_log->mutable_typed_config()->PackFrom(file_access_log);
    return config;
  }

  void setup(uint32_t connections) { setup(connections, false, defaultConfig()); }

  void setup(uint32_t connections,
             const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) {
    setup(connections, false, config);
  }

  void setup(uint32_t connections, bool set_redirect_records) {
    setup(connections, set_redirect_records, defaultConfig());
  }

  virtual void
  setup(uint32_t connections, bool set_redirect_records,
        const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) PURE;

  void raiseEventUpstreamConnected(uint32_t conn_index) {
    EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
    EXPECT_CALL(*upstream_connection_data_.at(conn_index), addUpstreamCallbacks(_))
        .WillOnce(Invoke([=](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;

          // Simulate TCP conn pool upstream callbacks. This is safe because the TCP proxy never
          // releases a connection so all events go to the same UpstreamCallbacks instance.
          upstream_connections_.at(conn_index)->addConnectionCallbacks(cb);
        }));
    EXPECT_CALL(*upstream_connections_.at(conn_index), enableHalfClose(true));
    conn_pool_callbacks_.at(conn_index)
        ->onPoolReady(std::move(upstream_connection_data_.at(conn_index)),
                      upstream_hosts_.at(conn_index));
  }

  void raiseEventUpstreamConnectFailed(
      uint32_t conn_index, ConnectionPool::PoolFailureReason reason,
      absl::optional<absl::string_view> failure_message = absl::nullopt) {
    conn_pool_callbacks_.at(conn_index)
        ->onPoolFailure(reason, failure_message ? *failure_message : "",
                        upstream_hosts_.at(conn_index));
  }

  Tcp::ConnectionPool::Cancellable* onNewConnection(Tcp::ConnectionPool::Cancellable* connection) {
    if (!new_connection_functions_.empty()) {
      auto fn = new_connection_functions_.front();
      new_connection_functions_.pop_front();
      return fn(connection);
    }
    return connection;
  }

  Event::TestTimeSystem& timeSystem() {
    return factory_context_.server_factory_context_.timeSystem();
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
  std::vector<std::shared_ptr<NiceMock<Upstream::MockHost>>> upstream_hosts_{};
  std::vector<std::unique_ptr<NiceMock<Network::MockClientConnection>>> upstream_connections_{};
  std::vector<std::unique_ptr<NiceMock<Tcp::ConnectionPool::MockConnectionData>>>
      upstream_connection_data_{};
  std::vector<Tcp::ConnectionPool::Callbacks*> conn_pool_callbacks_;
  std::vector<std::unique_ptr<NiceMock<Envoy::ConnectionPool::MockCancellable>>> conn_pool_handles_;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_;
  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_;
  StringViewSaver access_log_data_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  std::list<std::function<Tcp::ConnectionPool::Cancellable*(Tcp::ConnectionPool::Cancellable*)>>
      new_connection_functions_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  Upstream::ClusterInfoConstSharedPtr upstream_cluster_{};
  std::string redirect_records_data_ = "some data";
};

} // namespace TcpProxy
} // namespace Envoy
