#pragma once

#include "test/integration/fake_upstream.h"

namespace Envoy {

class AutonomousUpstream;

// A stream which automatically responds when the downstream request is
// completely read. By default the response is 200: OK with 10 bytes of
// payload. This behavior can be overridden with custom request headers defined below.
class AutonomousStream : public FakeStream {
public:
  // The number of response bytes to send. Payload is randomized.
  static const char RESPONSE_SIZE_BYTES[];
  // The number of data blocks send.
  static const char RESPONSE_DATA_BLOCKS[];
  // If set to an integer, the AutonomousStream will expect the response body to
  // be this large.
  static const char EXPECT_REQUEST_SIZE_BYTES[];
  // If set, the stream will reset when the request is complete, rather than
  // sending a response.
  static const char RESET_AFTER_REQUEST[];
  // Prevents upstream from sending trailers.
  static const char NO_TRAILERS[];
  // Prevents upstream from finishing response.
  static const char NO_END_STREAM[];
  // Closes the underlying connection after a given response is sent.
  static const char CLOSE_AFTER_RESPONSE[];

  AutonomousStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                   AutonomousUpstream& upstream, bool allow_incomplete_streams);
  ~AutonomousStream() override;

  void setEndStream(bool set) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) override;

private:
  AutonomousUpstream& upstream_;
  void sendResponse() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  const bool allow_incomplete_streams_{false};
  std::unique_ptr<Http::MetadataMapVector> pre_response_headers_metadata_;
};

// An upstream which creates AutonomousStreams for new incoming streams.
class AutonomousHttpConnection : public FakeHttpConnection {
public:
  AutonomousHttpConnection(AutonomousUpstream& autonomous_upstream,
                           SharedConnectionWrapper& shared_connection, Http::CodecType type,
                           uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
                           AutonomousUpstream& upstream);

  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder, bool) override;

private:
  AutonomousUpstream& upstream_;
  std::vector<FakeStreamPtr> streams_;
};

using AutonomousHttpConnectionPtr = std::unique_ptr<AutonomousHttpConnection>;

// An upstream which creates AutonomousHttpConnection for new incoming connections.
class AutonomousUpstream : public FakeUpstream {
public:
  AutonomousUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                     const Network::Address::InstanceConstSharedPtr& address,
                     const FakeUpstreamConfig& config, bool allow_incomplete_streams)
      : FakeUpstream(std::move(transport_socket_factory), address, config),
        allow_incomplete_streams_(allow_incomplete_streams),
        response_trailers_(std::make_unique<Http::TestResponseTrailerMapImpl>()),
        response_headers_(std::make_unique<Http::TestResponseHeaderMapImpl>(
            Http::TestResponseHeaderMapImpl({{":status", "200"}}))) {}

  AutonomousUpstream(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                     uint32_t port, Network::Address::IpVersion version,
                     const FakeUpstreamConfig& config, bool allow_incomplete_streams)
      : FakeUpstream(std::move(transport_socket_factory), port, version, config),
        allow_incomplete_streams_(allow_incomplete_streams),
        response_trailers_(std::make_unique<Http::TestResponseTrailerMapImpl>()),
        response_headers_(std::make_unique<Http::TestResponseHeaderMapImpl>(
            Http::TestResponseHeaderMapImpl({{":status", "200"}}))) {}

  ~AutonomousUpstream() override;
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const Filter::NetworkFilterFactoriesList& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager& listener) override;
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager& listener,
                                    Network::UdpReadFilterCallbacks& callbacks) override;
  bool createQuicListenerFilterChain(Network::QuicListenerFilterManager& listener) override;
  AssertionResult closeConnection(uint32_t index,
                                  std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  void setLastRequestHeaders(const Http::HeaderMap& headers);
  std::unique_ptr<Http::TestRequestHeaderMapImpl> lastRequestHeaders();
  void setResponseTrailers(std::unique_ptr<Http::TestResponseTrailerMapImpl>&& response_trailers);
  void setResponseBody(std::string body);
  void setResponseHeaders(std::unique_ptr<Http::TestResponseHeaderMapImpl>&& response_headers);
  void setPreResponseHeadersMetadata(std::unique_ptr<Http::MetadataMapVector>&& metadata);
  Http::TestResponseTrailerMapImpl responseTrailers();
  absl::optional<std::string> responseBody();
  Http::TestResponseHeaderMapImpl responseHeaders();
  std::unique_ptr<Http::MetadataMapVector> preResponseHeadersMetadata();
  const bool allow_incomplete_streams_{false};

private:
  Thread::MutexBasicLockable headers_lock_;
  std::unique_ptr<Http::TestRequestHeaderMapImpl> last_request_headers_;
  std::unique_ptr<Http::TestResponseTrailerMapImpl> response_trailers_;
  absl::optional<std::string> response_body_;
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers_;
  std::unique_ptr<Http::MetadataMapVector> pre_response_headers_metadata_;
  std::vector<AutonomousHttpConnectionPtr> http_connections_;
  std::vector<SharedConnectionWrapperPtr> shared_connections_;
};

} // namespace Envoy
