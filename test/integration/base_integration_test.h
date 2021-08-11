#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/server/process_context.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/config/version_converter.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration_tcp_client.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"

#include "absl/types/optional.h"
#include "spdlog/spdlog.h"

namespace Envoy {

struct ApiFilesystemConfig {
  std::string bootstrap_path_;
  std::string cds_path_;
  std::string eds_path_;
  std::string lds_path_;
  std::string rds_path_;
};

/**
 * Test fixture for all integration tests.
 */
class BaseIntegrationTest : protected Logger::Loggable<Logger::Id::testing> {
public:
  using InstanceConstSharedPtrFn = std::function<Network::Address::InstanceConstSharedPtr(int)>;

  // Creates a test fixture with an upstream bound to INADDR_ANY on an unspecified port using the
  // provided IP |version|.
  BaseIntegrationTest(Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::httpProxyConfig());
  // Creates a test fixture with a specified |upstream_address| function that provides the IP and
  // port to use.
  BaseIntegrationTest(const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::httpProxyConfig());
  virtual ~BaseIntegrationTest() = default;

  // Initialize the basic proto configuration, create fake upstreams, and start Envoy.
  virtual void initialize();
  // Set up the fake upstream connections. This is called by initialize() and
  // is virtual to allow subclass overrides.
  virtual void createUpstreams();
  // Finalize the config and spin up an Envoy instance.
  virtual void createEnvoy();
  // Sets upstream_protocol_ and alters the upstream protocol in the config_helper_
  void setUpstreamProtocol(Http::CodecType protocol);
  // Sets fake_upstreams_count_
  void setUpstreamCount(uint32_t count) { fake_upstreams_count_ = count; }
  // Skip validation that ensures that all upstream ports are referenced by the
  // configuration generated in ConfigHelper::finalize.
  void skipPortUsageValidation() { config_helper_.skipPortUsageValidation(); }
  // Make test more deterministic by using a fixed RNG value.
  void setDeterministic() { deterministic_ = true; }

  Http::CodecType upstreamProtocol() const { return upstream_config_.upstream_protocol_; }

  IntegrationTcpClientPtr
  makeTcpConnection(uint32_t port,
                    const Network::ConnectionSocket::OptionsSharedPtr& options = nullptr,
                    Network::Address::InstanceConstSharedPtr source_address =
                        Network::Address::InstanceConstSharedPtr());

  // Test-wide port map.
  void registerPort(const std::string& key, uint32_t port);
  uint32_t lookupPort(const std::string& key);

  // Set the endpoint's socket address to point at upstream at given index.
  void setUpstreamAddress(uint32_t upstream_index,
                          envoy::config::endpoint::v3::LbEndpoint& endpoint) const;

  Network::ClientConnectionPtr makeClientConnection(uint32_t port);
  virtual Network::ClientConnectionPtr
  makeClientConnectionWithOptions(uint32_t port,
                                  const Network::ConnectionSocket::OptionsSharedPtr& options);

  void registerTestServerPorts(const std::vector<std::string>& port_names);
  void createGeneratedApiTestServer(const std::string& bootstrap_path,
                                    const std::vector<std::string>& port_names,
                                    Server::FieldValidationConfig validator_config,
                                    bool allow_lds_rejection);
  void createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                           const std::vector<std::string>& port_names,
                           Server::FieldValidationConfig validator_config,
                           bool allow_lds_rejection);

  Event::TestTimeSystem& timeSystem() { return time_system_; }

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Api::ApiPtr api_for_server_stat_store_;
  MockBufferFactory* mock_buffer_factory_; // Will point to the dispatcher's factory.

  // Enable the listener access log
  void useListenerAccessLog(absl::string_view format = "");
  // Waits for the nth access log entry, defaulting to log entry 0.
  std::string waitForAccessLog(const std::string& filename, uint32_t entry = 0);

  std::string listener_access_log_name_;

  // Last node received on an xDS stream from the server.
  envoy::config::core::v3::Node last_node_;

  // Functions for testing reloadable config (xDS)
  void createXdsUpstream();
  void createXdsConnection();
  void cleanUpXdsConnection();

  // See if a port can be successfully bound within the given timeout.
  ABSL_MUST_USE_RESULT AssertionResult waitForPortAvailable(
      uint32_t port, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Helpers for setting up expectations and making the internal gears turn for xDS request/response
  // sending/receiving to/from the (imaginary) xDS server. You should almost always use
  // compareDiscoveryRequest() and sendDiscoveryResponse(), but the SotW/delta-specific versions are
  // available if you're writing a SotW/delta-specific test.
  AssertionResult compareDiscoveryRequest(
      const std::string& expected_type_url, const std::string& expected_version,
      const std::vector<std::string>& expected_resource_names,
      const std::vector<std::string>& expected_resource_names_added,
      const std::vector<std::string>& expected_resource_names_removed, bool expect_node = false,
      const Protobuf::int32 expected_error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
      const std::string& expected_error_message = "");
  template <class T>
  void sendDiscoveryResponse(const std::string& type_url, const std::vector<T>& state_of_the_world,
                             const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed, const std::string& version,
                             const bool api_downgrade = false) {
    if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
      sendSotwDiscoveryResponse(type_url, state_of_the_world, version, api_downgrade);
    } else {
      sendDeltaDiscoveryResponse(type_url, added_or_updated, removed, version, api_downgrade);
    }
  }

  AssertionResult compareDeltaDiscoveryRequest(
      const std::string& expected_type_url,
      const std::vector<std::string>& expected_resource_subscriptions,
      const std::vector<std::string>& expected_resource_unsubscriptions,
      const Protobuf::int32 expected_error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
      const std::string& expected_error_message = "") {
    return compareDeltaDiscoveryRequest(expected_type_url, expected_resource_subscriptions,
                                        expected_resource_unsubscriptions, xds_stream_,
                                        expected_error_code, expected_error_message);
  }

  AssertionResult compareDeltaDiscoveryRequest(
      const std::string& expected_type_url,
      const std::vector<std::string>& expected_resource_subscriptions,
      const std::vector<std::string>& expected_resource_unsubscriptions, FakeStreamPtr& stream,
      const Protobuf::int32 expected_error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
      const std::string& expected_error_message = "");

  AssertionResult compareSotwDiscoveryRequest(
      const std::string& expected_type_url, const std::string& expected_version,
      const std::vector<std::string>& expected_resource_names, bool expect_node = false,
      const Protobuf::int32 expected_error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
      const std::string& expected_error_message = "");

  template <class T>
  void sendSotwDiscoveryResponse(const std::string& type_url, const std::vector<T>& messages,
                                 const std::string& version, const bool api_downgrade = false) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info(version);
    discovery_response.set_type_url(type_url);
    for (const auto& message : messages) {
      if (api_downgrade) {
        discovery_response.add_resources()->PackFrom(API_DOWNGRADE(message));
      } else {
        discovery_response.add_resources()->PackFrom(message);
      }
    }
    static int next_nonce_counter = 0;
    discovery_response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));
    xds_stream_->sendGrpcMessage(discovery_response);
  }

  template <class T>
  void sendDeltaDiscoveryResponse(const std::string& type_url,
                                  const std::vector<T>& added_or_updated,
                                  const std::vector<std::string>& removed,
                                  const std::string& version, const bool api_downgrade = false) {
    sendDeltaDiscoveryResponse(type_url, added_or_updated, removed, version, xds_stream_, {},
                               api_downgrade);
  }
  template <class T>
  void
  sendDeltaDiscoveryResponse(const std::string& type_url, const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed, const std::string& version,
                             FakeStreamPtr& stream, const std::vector<std::string>& aliases = {},
                             const bool api_downgrade = false) {
    auto response = createDeltaDiscoveryResponse<T>(type_url, added_or_updated, removed, version,
                                                    aliases, api_downgrade);
    stream->sendGrpcMessage(response);
  }

  template <class T>
  envoy::service::discovery::v3::DeltaDiscoveryResponse
  createDeltaDiscoveryResponse(const std::string& type_url, const std::vector<T>& added_or_updated,
                               const std::vector<std::string>& removed, const std::string& version,
                               const std::vector<std::string>& aliases,
                               const bool api_downgrade = false) {

    envoy::service::discovery::v3::DeltaDiscoveryResponse response;
    response.set_system_version_info("system_version_info_this_is_a_test");
    response.set_type_url(type_url);
    for (const auto& message : added_or_updated) {
      auto* resource = response.add_resources();
      ProtobufWkt::Any temp_any;
      if (api_downgrade) {
        temp_any.PackFrom(API_DOWNGRADE(message));
        resource->mutable_resource()->PackFrom(API_DOWNGRADE(message));
      } else {
        temp_any.PackFrom(message);
        resource->mutable_resource()->PackFrom(message);
      }
      resource->set_name(intResourceName(message));
      resource->set_version(version);
      for (const auto& alias : aliases) {
        resource->add_aliases(alias);
      }
    }
    *response.mutable_removed_resources() = {removed.begin(), removed.end()};
    static int next_nonce_counter = 0;
    response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));
    return response;
  }

private:
  template <class T> std::string intResourceName(const T& m) {
    // gcc doesn't allow inline template function to be specialized, using a constexpr if to
    // workaround.
    if constexpr (std::is_same_v<T, envoy::config::endpoint::v3::ClusterLoadAssignment>) {
      return m.cluster_name();
    } else {
      return m.name();
    }
  }

  Event::GlobalTimeSystem time_system_;

public:
  Event::DispatcherPtr dispatcher_;

  /**
   * Open a connection to Envoy, send a series of bytes, and return the
   * response. This function will continue reading response bytes until Envoy
   * closes the connection (as a part of error handling) or (if configured true)
   * the complete headers are read.
   *
   * @param port the port to connect to.
   * @param raw_http the data to send.
   * @param response the response data will be sent here
   * @param if the connection should be terminated once '\r\n\r\n' has been read.
   **/
  void sendRawHttpAndWaitForResponse(int port, const char* raw_http, std::string* response,
                                     bool disconnect_after_headers_complete = false,
                                     Network::TransportSocketPtr transport_socket = nullptr);

  /**
   * Helper to create ConnectionDriver.
   *
   * @param port the port to connect to.
   * @param initial_data the data to send.
   * @param data_callback the callback on the received data.
   **/
  std::unique_ptr<RawConnectionDriver> createConnectionDriver(
      uint32_t port, const std::string& initial_data,
      std::function<void(Network::ClientConnection&, const Buffer::Instance&)>&& data_callback,
      Network::TransportSocketPtr transport_socket = nullptr) {
    Buffer::OwnedImpl buffer(initial_data);
    return std::make_unique<RawConnectionDriver>(port, buffer, data_callback, version_,
                                                 *dispatcher_, std::move(transport_socket));
  }

  /**
   * Helper to create ConnectionDriver.
   *
   * @param port the port to connect to.
   * @param write_request_cb callback used to send data.
   * @param data_callback the callback on the received data.
   * @param transport_socket transport socket to use for the client connection
   **/
  std::unique_ptr<RawConnectionDriver> createConnectionDriver(
      uint32_t port, RawConnectionDriver::DoWriteCallback write_request_cb,
      std::function<void(Network::ClientConnection&, const Buffer::Instance&)>&& data_callback,
      Network::TransportSocketPtr transport_socket = nullptr) {
    return std::make_unique<RawConnectionDriver>(port, write_request_cb, data_callback, version_,
                                                 *dispatcher_, std::move(transport_socket));
  }

  FakeUpstreamConfig configWithType(Http::CodecType type) const {
    FakeUpstreamConfig config = upstream_config_;
    config.upstream_protocol_ = type;
    if (type != Http::CodecType::HTTP3) {
      config.udp_fake_upstream_ = absl::nullopt;
    }
    return config;
  }

  FakeUpstream& addFakeUpstream(Http::CodecType type) {
    auto config = configWithType(type);
    fake_upstreams_.emplace_back(std::make_unique<FakeUpstream>(0, version_, config));
    return *fake_upstreams_.back();
  }

  FakeUpstream& addFakeUpstream(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                                Http::CodecType type) {
    auto config = configWithType(type);
    fake_upstreams_.emplace_back(
        std::make_unique<FakeUpstream>(std::move(transport_socket_factory), 0, version_, config));
    return *fake_upstreams_.back();
  }

protected:
  void setUdpFakeUpstream(absl::optional<FakeUpstreamConfig::UdpConfig> config) {
    upstream_config_.udp_fake_upstream_ = config;
  }
  bool initialized() const { return initialized_; }

  // Right now half-close is set globally, not separately for upstream and
  // downstream.
  void enableHalfClose(bool value) { upstream_config_.enable_half_close_ = value; }

  bool enableHalfClose() { return upstream_config_.enable_half_close_; }

  const FakeUpstreamConfig& upstreamConfig() { return upstream_config_; }
  void setMaxRequestHeadersKb(uint32_t value) { upstream_config_.max_request_headers_kb_ = value; }
  void setMaxRequestHeadersCount(uint32_t value) {
    upstream_config_.max_request_headers_count_ = value;
  }
  void setHeadersWithUnderscoreAction(
      envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction value) {
    upstream_config_.headers_with_underscores_action_ = value;
  }

  void setServerBufferFactory(Buffer::WatermarkFactorySharedPtr proxy_buffer_factory) {
    ASSERT(!test_server_, "Proxy buffer factory must be set before test server creation");
    proxy_buffer_factory_ = proxy_buffer_factory;
  }

  void mergeOptions(envoy::config::core::v3::Http2ProtocolOptions& options) {
    upstream_config_.http2_options_.MergeFrom(options);
  }

  std::unique_ptr<Stats::Scope> upstream_stats_store_;

  // Make sure the test server will be torn down after any fake client.
  // The test server owns the runtime, which is often accessed by client and
  // fake upstream codecs and must outlast them.
  IntegrationTestServerPtr test_server_;

  // The IpVersion (IPv4, IPv6) to use.
  Network::Address::IpVersion version_;
  // IP Address to use when binding sockets on upstreams.
  InstanceConstSharedPtrFn upstream_address_fn_;
  // The config for envoy start-up.
  ConfigHelper config_helper_;
  // The ProcessObject to use when constructing the envoy server.
  ProcessObjectOptRef process_object_{absl::nullopt};

  // Steps that should be done before the envoy server starting.
  std::function<void(IntegrationTestServer&)> on_server_ready_function_;

  // Steps that should be done in parallel with the envoy server starting. E.g., xDS
  // pre-init, control plane synchronization needed for server start.
  std::function<void()> on_server_init_function_;

  // A map of keys to port names. Generally the names are pulled from the v2 listener name
  // but if a listener is created via ADS, it will be from whatever key is used with registerPort.
  TestEnvironment::PortMap port_map_;

  // The DrainStrategy that dictates the behaviour of
  // DrainManagerImpl::drainClose().
  Server::DrainStrategy drain_strategy_{Server::DrainStrategy::Gradual};

  // Member variables for xDS testing.
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
  bool create_xds_upstream_{false};
  bool tls_xds_upstream_{false};
  bool use_lds_{true}; // Use the integration framework's LDS set up.
  bool upstream_tls_{false};

  Network::TransportSocketFactoryPtr
  createUpstreamTlsContext(const FakeUpstreamConfig& upstream_config);
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{timeSystem()};

  // The fake upstreams_ are created using the context_manager, so make sure
  // they are destroyed before it is.
  std::vector<std::unique_ptr<FakeUpstream>> fake_upstreams_;

  Grpc::SotwOrDelta sotw_or_delta_{Grpc::SotwOrDelta::Sotw};

  spdlog::level::level_enum default_log_level_;

  // Target number of upstreams.
  uint32_t fake_upstreams_count_{1};

  // The duration of the drain manager graceful drain period.
  std::chrono::seconds drain_time_{1};

  // The number of worker threads that the test server uses.
  uint32_t concurrency_{1};

  // If true, use AutonomousUpstream for fake upstreams.
  bool autonomous_upstream_{false};

  // If true, allow incomplete streams in AutonomousUpstream
  // This does nothing if autonomous_upstream_ is false
  bool autonomous_allow_incomplete_streams_{false};

  // True if test will use a fixed RNG value.
  bool deterministic_{};

  // Set true when your test will itself take care of ensuring listeners are up, and registering
  // them in the port_map_.
  bool defer_listener_finalization_{false};

  // By default the test server will use custom stats to notify on increment.
  // This override exists for tests measuring stats memory.
  bool use_real_stats_{};

  // Use a v2 bootstrap.
  bool v2_bootstrap_{false};

private:
  // Configuration for the fake upstream.
  FakeUpstreamConfig upstream_config_{time_system_};
  // True if initialized() has been called.
  bool initialized_{};
  // Optional factory that the proxy-under-test should use to create watermark buffers. If nullptr,
  // the proxy uses the default watermark buffer factory to create buffers.
  Buffer::WatermarkFactorySharedPtr proxy_buffer_factory_;
};

} // namespace Envoy
