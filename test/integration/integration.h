#pragma once

#include "fake_upstream.h"
#include "server.h"

#include "common/http/codec_client.h"
#include "common/network/filter_impl.h"
#include "common/stats/stats_impl.h"

/**
 * Stream decoder wrapper used during integration testing.
 */
class IntegrationStreamDecoder : public Http::StreamDecoder, public Http::StreamCallbacks {
public:
  IntegrationStreamDecoder(Event::Dispatcher& dispatcher);

  const std::string& body() { return body_; }
  bool complete() { return saw_end_stream_; }
  const Http::HeaderMap& headers() { return *headers_; }
  const Http::HeaderMapPtr& trailers() { return trailers_; }
  void waitForBodyData(uint64_t size);
  void waitForEndStream();
  void waitForReset();

  // Http::StreamDecoder
  void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(const Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason) override;

private:
  Event::Dispatcher& dispatcher_;
  Http::HeaderMapPtr headers_;
  Http::HeaderMapPtr trailers_;
  bool waiting_for_end_stream_{};
  bool saw_end_stream_{};
  std::string body_;
  uint64_t body_data_waiting_length_{};
  bool waiting_for_reset_{};
};

typedef std::unique_ptr<IntegrationStreamDecoder> IntegrationStreamDecoderPtr;

/**
 * HTTP codec client used during integration testing.
 */
class IntegrationCodecClient : public Http::CodecClientProd {
public:
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
                         const Http::CodecClientStats& stats, Stats::Store& store,
                         Http::CodecClient::Type type);

  void makeHeaderOnlyRequest(const Http::HeaderMap& headers, IntegrationStreamDecoder& response);
  void makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size,
                           IntegrationStreamDecoder& response);
  bool sawGoAway() { return saw_goaway_; }
  void sendData(Http::StreamEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::StreamEncoder& encoder, const Http::HeaderMap& trailers);
  void sendReset(Http::StreamEncoder& encoder);
  Http::StreamEncoder& startRequest(const Http::HeaderMap& headers,
                                    IntegrationStreamDecoder& response);
  void waitForDisconnect();

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}
    void onEvent(uint32_t events) override;

    IntegrationCodecClient& parent_;
  };

  struct CodecCallbacks : public Http::ConnectionCallbacks {
    CodecCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::ConnectionCallbacks
    void onGoAway() override { parent_.saw_goaway_ = true; }

    IntegrationCodecClient& parent_;
  };

  void flushWrite();

  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
};

typedef std::unique_ptr<IntegrationCodecClient> IntegrationCodecClientPtr;

/**
 * TCP client used during integration testing.
 */
class IntegrationTcpClient {
public:
  IntegrationTcpClient(Event::Dispatcher& dispatcher, uint32_t port);

  void close();
  const std::string& data() { return data_; }
  void waitForData(const std::string& data);
  void waitForDisconnect();
  void write(const std::string& data);

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks,
                               public Network::ReadFilterBaseImpl {
    ConnectionCallbacks(IntegrationTcpClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onBufferChange(Network::ConnectionBufferType, uint64_t, int64_t) override {}
    void onEvent(uint32_t events) override;

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data) override;

    IntegrationTcpClient& parent_;
  };

  std::shared_ptr<ConnectionCallbacks> callbacks_;
  Network::ClientConnectionPtr connection_;
  bool disconnected_{};
  std::string data_;
  std::string data_to_wait_for_;
};

typedef std::unique_ptr<IntegrationTcpClient> IntegrationTcpClientPtr;

/**
 * Test fixture for all integration tests.
 */
class BaseIntegrationTest : Logger::Loggable<Logger::Id::testing> {
public:
  static const uint32_t ECHO_PORT = 10000;
  static const uint32_t HTTP_PORT = 10001;
  static const uint32_t HTTP_BUFFER_PORT = 10002;
  static const uint32_t ADMIN_PORT = 10003;
  static const uint32_t TCP_PROXY_PORT = 10004;

  BaseIntegrationTest();
  ~BaseIntegrationTest();

  /**
   * Integration tests are composed of a sequence of actions which are run via this routine.
   */
  void executeActions(std::list<std::function<void()>> actions) {
    for (std::function<void()> action : actions) {
      action();
    }
  }

  Network::ClientConnectionPtr makeClientConnection(uint32_t port);

  IntegrationCodecClientPtr makeHttpConnection(uint32_t port, Http::CodecClient::Type type);
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn,
                                               Http::CodecClient::Type type);
  IntegrationTcpClientPtr makeTcpConnection(uint32_t port);

  static IntegrationTestServerPtr test_server_;
  static std::vector<std::unique_ptr<FakeUpstream>> fake_upstreams_;
  static spdlog::level::level_enum default_log_level_;

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;

protected:
  void testRouterRedirect(Http::CodecClient::Type type);
  void testRouterNotFound(Http::CodecClient::Type type);
  void testRouterRequestAndResponseWithBody(Network::ClientConnectionPtr&& conn,
                                            Http::CodecClient::Type type, uint64_t request_size,
                                            uint64_t response_size);
  void testRouterHeaderOnlyRequestAndResponse(Network::ClientConnectionPtr&& conn,
                                              Http::CodecClient::Type type);
  void testRouterUpstreamDisconnectBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                         Http::CodecClient::Type type);
  void testRouterUpstreamDisconnectBeforeResponseComplete(Network::ClientConnectionPtr&& conn,
                                                          Http::CodecClient::Type type);
  void testRouterDownstreamDisconnectBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                           Http::CodecClient::Type type);
  void testRouterDownstreamDisconnectBeforeResponseComplete(Network::ClientConnectionPtr&& conn,
                                                            Http::CodecClient::Type type);
  void testRouterUpstreamResponseBeforeRequestComplete(Network::ClientConnectionPtr&& conn,
                                                       Http::CodecClient::Type type);
  void testTwoRequests(Http::CodecClient::Type type);
  void testBadHttpRequest();
  void testHttp10Request();
  void testNoHost();
  void testUpstreamProtocolError();
  void testBadPath();
  void testDrainClose(Http::CodecClient::Type type);
  void testRetry(Http::CodecClient::Type type);

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  void testTrailers(uint64_t request_size, uint64_t response_size);
};

class IntegrationTest : public BaseIntegrationTest, public testing::Test {
public:
  /**
   * Global initializer for all integration tests.
   */
  static void SetUpTestCase() {
    test_server_ = IntegrationTestServer::create("test/config/integration/server.json");
    fake_upstreams_.emplace_back(new FakeUpstream(11000, FakeHttpConnection::Type::HTTP1));
    fake_upstreams_.emplace_back(new FakeUpstream(11001, FakeHttpConnection::Type::HTTP1));
  }

  /**
   * Global destructor for all integration tests.
   */
  static void TearDownTestCase() {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};
