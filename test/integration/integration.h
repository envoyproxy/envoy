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
  const Http::HeaderMap& headers() { return *headers_; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForHeaders();
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();

  // Http::StreamDecoder
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Event::Dispatcher& dispatcher_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
  bool waiting_for_end_stream_{};
  bool saw_end_stream_{};
  std::string body_;
  uint64_t body_data_waiting_length_{};
  bool waiting_for_reset_{};
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
                       Network::Address::IpVersion version);

  void close();
  void waitForData(const std::string& data);
  void waitForDisconnect();
  void write(const std::string& data);
  const std::string& data() { return payload_reader_->data(); }

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
  BaseIntegrationTest(Network::Address::IpVersion version);
  virtual ~BaseIntegrationTest() {}

  virtual void initialize() {
    RELEASE_ASSERT(!initialized_);
    initialized_ = true;
  }

  IntegrationTcpClientPtr makeTcpConnection(uint32_t port);

  // Test-wide port map.
  void registerPort(const std::string& key, uint32_t port);
  uint32_t lookupPort(const std::string& key);

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
  void sendRawHttpAndWaitForResponse(const char* http, std::string* response);

protected:
  // The IpVersion (IPv4, IPv6) to use.
  Network::Address::IpVersion version_;
  // The config for envoy start-up.
  ConfigHelper config_helper_{version_};

  std::vector<std::unique_ptr<FakeUpstream>> fake_upstreams_;
  spdlog::level::level_enum default_log_level_;
  IntegrationTestServerPtr test_server_;
  TestEnvironment::PortMap port_map_;
  bool initialized_{}; // True if initialized() has been called.
};

} // namespace Envoy
