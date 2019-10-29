#pragma once

#include <functional>
#include <list>
#include <string>
#include <vector>

#include "envoy/server/process_context.h"

#include "common/http/codec_client.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"

#include "absl/types/optional.h"
#include "spdlog/spdlog.h"

namespace Envoy {
/**
 * Stream decoder wrapper used during integration testing.
 */
class IntegrationStreamDecoder : public Http::StreamDecoder, public Http::StreamCallbacks {
public:
  IntegrationStreamDecoder(Event::Dispatcher& dispatcher);

  const std::string& body() { return body_; }
  bool complete() { return saw_end_stream_; }
  bool reset() { return saw_reset_; }
  Http::StreamResetReason reset_reason() { return reset_reason_; }
  const Http::HeaderMap* continue_headers() { return continue_headers_.get(); }
  const Http::HeaderMap& headers() { return *headers_; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  const Http::MetadataMap& metadata_map() { return *metadata_map_; }
  uint64_t keyCount(std::string key) { return duplicated_metadata_key_count_[key]; }
  void waitForContinueHeaders();
  void waitForHeaders();
  // This function waits until body_ has at least size bytes in it (it might have more). clearBody()
  // can be used if the previous body data is not relevant and the test wants to wait for a specific
  // amount of new data without considering the existing body size.
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();
  void clearBody() { body_.clear(); }

  // Http::StreamDecoder
  void decode100ContinueHeaders(Http::HeaderMapPtr&& headers) override;
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  Http::HeaderMapPtr continue_headers_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
  Http::MetadataMapPtr metadata_map_{new Http::MetadataMap()};
  std::unordered_map<std::string, uint64_t> duplicated_metadata_key_count_;
  bool waiting_for_end_stream_{};
  bool saw_end_stream_{};
  std::string body_;
  uint64_t body_data_waiting_length_{};
  bool waiting_for_reset_{};
  bool waiting_for_continue_headers_{};
  bool waiting_for_headers_{};
  bool saw_reset_{};
  Http::StreamResetReason reset_reason_{};
};

using IntegrationStreamDecoderPtr = std::unique_ptr<IntegrationStreamDecoder>;

/**
 * TCP client used during integration testing.
 */
class IntegrationTcpClient {
public:
  IntegrationTcpClient(Event::Dispatcher& dispatcher, MockBufferFactory& factory, uint32_t port,
                       Network::Address::IpVersion version, bool enable_half_close = false);

  void close();
  void waitForData(const std::string& data, bool exact_match = true);
  // wait for at least `length` bytes to be received
  void waitForData(size_t length);
  void waitForDisconnect(bool ignore_spurious_events = false);
  void waitForHalfClose();
  void readDisable(bool disabled);
  void write(const std::string& data, bool end_stream = false, bool verify = true);
  const std::string& data() { return payload_reader_->data(); }
  bool connected() const { return !disconnected_; }
  // clear up to the `count` number of bytes of received data
  void clearData(size_t count = std::string::npos) { payload_reader_->clearData(count); }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationTcpClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationTcpClient& parent_;
  };

  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  std::shared_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr connection_;
  bool disconnected_{};
  MockWatermarkBuffer* client_write_buffer_;
};

using IntegrationTcpClientPtr = std::unique_ptr<IntegrationTcpClient>;

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
class BaseIntegrationTest : Logger::Loggable<Logger::Id::testing> {
public:
  using TestTimeSystemPtr = std::unique_ptr<Event::TestTimeSystem>;
  using InstanceConstSharedPtrFn = std::function<Network::Address::InstanceConstSharedPtr(int)>;

  // Creates a test fixture with an upstream bound to INADDR_ANY on an unspecified port using the
  // provided IP |version|.
  BaseIntegrationTest(Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);
  BaseIntegrationTest(Network::Address::IpVersion version, TestTimeSystemPtr,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG)
      : BaseIntegrationTest(version, config) {}
  // Creates a test fixture with a specified |upstream_address| function that provides the IP and
  // port to use.
  BaseIntegrationTest(const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);

  virtual ~BaseIntegrationTest() = default;

  // TODO(jmarantz): Remove this once
  // https://github.com/envoyproxy/envoy-filter-example/pull/69 is reverted.
  static TestTimeSystemPtr realTime() { return TestTimeSystemPtr(); }

  // Initialize the basic proto configuration, create fake upstreams, and start Envoy.
  virtual void initialize();
  // Set up the fake upstream connections. This is called by initialize() and
  // is virtual to allow subclass overrides.
  virtual void createUpstreams();
  // Finalize the config and spin up an Envoy instance.
  virtual void createEnvoy();
  // Sets upstream_protocol_ and alters the upstream protocol in the config_helper_
  void setUpstreamProtocol(FakeHttpConnection::Type protocol);
  // Sets fake_upstreams_count_ and alters the upstream protocol in the config_helper_
  void setUpstreamCount(uint32_t count) { fake_upstreams_count_ = count; }
  // Skip validation that ensures that all upstream ports are referenced by the
  // configuration generated in ConfigHelper::finalize.
  void skipPortUsageValidation() { config_helper_.skipPortUsageValidation(); }
  // Make test more deterministic by using a fixed RNG value.
  void setDeterministic() { deterministic_ = true; }

  FakeHttpConnection::Type upstreamProtocol() const { return upstream_protocol_; }

  IntegrationTcpClientPtr makeTcpConnection(uint32_t port);

  // Test-wide port map.
  void registerPort(const std::string& key, uint32_t port);
  uint32_t lookupPort(const std::string& key);

  // Set the endpoint's socket address to point at upstream at given index.
  void setUpstreamAddress(uint32_t upstream_index,
                          envoy::api::v2::endpoint::LbEndpoint& endpoint) const;

  Network::ClientConnectionPtr makeClientConnection(uint32_t port);

  void registerTestServerPorts(const std::vector<std::string>& port_names);
  void createTestServer(const std::string& json_path, const std::vector<std::string>& port_names);
  void createGeneratedApiTestServer(const std::string& bootstrap_path,
                                    const std::vector<std::string>& port_names,
                                    bool allow_unknown_static_fields,
                                    bool reject_unknown_dynamic_fields, bool allow_lds_rejection);
  void createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                           const std::vector<std::string>& port_names,
                           bool allow_unknown_static_fields, bool reject_unknown_dynamic_fields,
                           bool allow_lds_rejection);

  Event::TestTimeSystem& timeSystem() { return time_system_; }

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Api::ApiPtr api_for_server_stat_store_;
  MockBufferFactory* mock_buffer_factory_; // Will point to the dispatcher's factory.

  // Functions for testing reloadable config (xDS)
  void createXdsUpstream();
  void createXdsConnection();
  void cleanUpXdsConnection();

  // Helpers for setting up expectations and making the internal gears turn for xDS request/response
  // sending/receiving to/from the (imaginary) xDS server. You should almost always use
  // compareDiscoveryRequest() and sendDiscoveryResponse(), but the SotW/delta-specific versions are
  // available if you're writing a SotW/delta-specific test.
  // TODO(fredlas) expect_node was defaulting false here; the delta+SotW unification work restores
  // it.
  AssertionResult
  compareDiscoveryRequest(const std::string& expected_type_url, const std::string& expected_version,
                          const std::vector<std::string>& expected_resource_names,
                          const std::vector<std::string>& expected_resource_names_added,
                          const std::vector<std::string>& expected_resource_names_removed,
                          bool expect_node = true,
                          const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
                          const std::string& expected_error_message = "");
  template <class T>
  void sendDiscoveryResponse(const std::string& type_url, const std::vector<T>& state_of_the_world,
                             const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed, const std::string& version) {
    if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
      sendSotwDiscoveryResponse(type_url, state_of_the_world, version);
    } else {
      sendDeltaDiscoveryResponse(type_url, added_or_updated, removed, version);
    }
  }

  AssertionResult compareDeltaDiscoveryRequest(
      const std::string& expected_type_url,
      const std::vector<std::string>& expected_resource_subscriptions,
      const std::vector<std::string>& expected_resource_unsubscriptions,
      const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
      const std::string& expected_error_message = "") {
    return compareDeltaDiscoveryRequest(expected_type_url, expected_resource_subscriptions,
                                        expected_resource_unsubscriptions, xds_stream_,
                                        expected_error_code, expected_error_message);
  }

  AssertionResult compareDeltaDiscoveryRequest(
      const std::string& expected_type_url,
      const std::vector<std::string>& expected_resource_subscriptions,
      const std::vector<std::string>& expected_resource_unsubscriptions, FakeStreamPtr& stream,
      const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
      const std::string& expected_error_message = "");

  // TODO(fredlas) expect_node was defaulting false here; the delta+SotW unification work restores
  // it.
  AssertionResult compareSotwDiscoveryRequest(
      const std::string& expected_type_url, const std::string& expected_version,
      const std::vector<std::string>& expected_resource_names, bool expect_node = true,
      const Protobuf::int32 expected_error_code = Grpc::Status::GrpcStatus::Ok,
      const std::string& expected_error_message = "");

  template <class T>
  void sendSotwDiscoveryResponse(const std::string& type_url, const std::vector<T>& messages,
                                 const std::string& version) {
    envoy::api::v2::DiscoveryResponse discovery_response;
    discovery_response.set_version_info(version);
    discovery_response.set_type_url(type_url);
    for (const auto& message : messages) {
      discovery_response.add_resources()->PackFrom(message);
    }
    static int next_nonce_counter = 0;
    discovery_response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));
    xds_stream_->sendGrpcMessage(discovery_response);
  }

  template <class T>
  void
  sendDeltaDiscoveryResponse(const std::string& type_url, const std::vector<T>& added_or_updated,
                             const std::vector<std::string>& removed, const std::string& version) {
    sendDeltaDiscoveryResponse(type_url, added_or_updated, removed, version, xds_stream_);
  }
  template <class T>
  void sendDeltaDiscoveryResponse(const std::string& type_url,
                                  const std::vector<T>& added_or_updated,
                                  const std::vector<std::string>& removed,
                                  const std::string& version, FakeStreamPtr& stream) {
    envoy::api::v2::DeltaDiscoveryResponse response;
    response.set_system_version_info("system_version_info_this_is_a_test");
    response.set_type_url(type_url);
    for (const auto& message : added_or_updated) {
      auto* resource = response.add_resources();
      ProtobufWkt::Any temp_any;
      temp_any.PackFrom(message);
      resource->set_name(TestUtility::xdsResourceName(temp_any));
      resource->set_version(version);
      resource->mutable_resource()->PackFrom(message);
    }
    *response.mutable_removed_resources() = {removed.begin(), removed.end()};
    static int next_nonce_counter = 0;
    response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));
    stream->sendGrpcMessage(response);
  }

private:
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
                                     bool disconnect_after_headers_complete = false);

protected:
  // Create the envoy server in another thread and start it.
  // Will not return until that server is listening.
  virtual IntegrationTestServerPtr
  createIntegrationTestServer(const std::string& bootstrap_path,
                              std::function<void()> on_server_init_function,
                              Event::TestTimeSystem& time_system);

  bool initialized() const { return initialized_; }

  std::unique_ptr<Stats::Scope> upstream_stats_store_;

  // The IpVersion (IPv4, IPv6) to use.
  Network::Address::IpVersion version_;
  // IP Address to use when binding sockets on upstreams.
  InstanceConstSharedPtrFn upstream_address_fn_;
  // The config for envoy start-up.
  ConfigHelper config_helper_;
  // The ProcessObject to use when constructing the envoy server.
  absl::optional<std::reference_wrapper<ProcessObject>> process_object_{absl::nullopt};

  // Steps that should be done in parallel with the envoy server starting. E.g., xDS
  // pre-init, control plane synchronization needed for server start.
  std::function<void()> on_server_init_function_;

  std::vector<std::unique_ptr<FakeUpstream>> fake_upstreams_;
  // Target number of upstreams.
  uint32_t fake_upstreams_count_{1};
  spdlog::level::level_enum default_log_level_;
  IntegrationTestServerPtr test_server_;
  // A map of keys to port names. Generally the names are pulled from the v2 listener name
  // but if a listener is created via ADS, it will be from whatever key is used with registerPort.
  TestEnvironment::PortMap port_map_;

  // If true, use AutonomousUpstream for fake upstreams.
  bool autonomous_upstream_{false};

  bool enable_half_close_{false};

  // Whether the default created fake upstreams are UDP listeners.
  bool udp_fake_upstream_{false};

  // True if test will use a fixed RNG value.
  bool deterministic_{};

  // Set true when your test will itself take care of ensuring listeners are up, and registering
  // them in the port_map_.
  bool defer_listener_finalization_{false};

  // The number of worker threads that the test server uses.
  uint32_t concurrency_{1};

  // Member variables for xDS testing.
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{timeSystem()};
  bool create_xds_upstream_{false};
  bool tls_xds_upstream_{false};
  bool use_lds_{true}; // Use the integration framework's LDS set up.
  Grpc::SotwOrDelta sotw_or_delta_{Grpc::SotwOrDelta::Sotw};

private:
  // The type for the Envoy-to-backend connection
  FakeHttpConnection::Type upstream_protocol_{FakeHttpConnection::Type::HTTP1};
  // True if initialized() has been called.
  bool initialized_{};
};

} // namespace Envoy
