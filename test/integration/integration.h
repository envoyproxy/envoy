#pragma once

#include <functional>
#include <list>
#include <string>
#include <vector>

#include "common/http/codec_client.h"

#include "test/config/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/integration/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"

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
  void waitForContinueHeaders();
  void waitForHeaders();
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();

  // Http::StreamDecoder
  void decode100ContinueHeaders(Http::HeaderMapPtr&& headers) override;
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  Http::HeaderMapPtr continue_headers_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
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

typedef std::unique_ptr<IntegrationStreamDecoder> IntegrationStreamDecoderPtr;

/**
 * TCP client used during integration testing.
 */
class IntegrationTcpClient {
public:
  IntegrationTcpClient(Event::Dispatcher& dispatcher, MockBufferFactory& factory, uint32_t port,
                       Network::Address::IpVersion version, bool enable_half_close = false);

  void close();
  void waitForData(const std::string& data);
  void waitForDisconnect(bool ignore_spurious_events = false);
  void waitForHalfClose();
  void readDisable(bool disabled);
  void write(const std::string& data, bool end_stream = false, bool verify = true);
  const std::string& data() { return payload_reader_->data(); }
  bool connected() const { return !disconnected_; }

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

typedef std::unique_ptr<IntegrationTcpClient> IntegrationTcpClientPtr;

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
  BaseIntegrationTest(Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);
  virtual ~BaseIntegrationTest() {}

  void SetUp();

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
                                    const std::vector<std::string>& port_names);
  void createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                           const std::vector<std::string>& port_names);

  Api::ApiPtr api_;
  MockBufferFactory* mock_buffer_factory_; // Will point to the dispatcher's factory.
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
   * @param if the connection should be terminated onece '\r\n\r\n' has been read.
   **/
  void sendRawHttpAndWaitForResponse(int port, const char* raw_http, std::string* response,
                                     bool disconnect_after_headers_complete = false);

protected:
  bool initialized() const { return initialized_; }

  // The IpVersion (IPv4, IPv6) to use.
  Network::Address::IpVersion version_;
  // The config for envoy start-up.
  ConfigHelper config_helper_;
  // Steps that should be done prior to the workers starting. E.g., xDS pre-init.
  std::function<void()> pre_worker_start_test_steps_;

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

private:
  // The codec type for the client-to-Envoy connection
  Http::CodecClient::Type downstream_protocol_{Http::CodecClient::Type::HTTP1};
  // The type for the Envoy-to-backend connection
  FakeHttpConnection::Type upstream_protocol_{FakeHttpConnection::Type::HTTP1};
  // True if initialized() has been called.
  bool initialized_{};
  // True if test will use a fixed RNG value.
  bool deterministic_{};
};

} // namespace Envoy
