#include "test/common/integration/autonomous_upstream.h"

namespace Envoy {

AutonomousStream::AutonomousStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                                   AutonomousUpstream& upstream)
    : FakeStream(parent, encoder, upstream.timeSystem()) {}

AutonomousStream::~AutonomousStream() {
  RELEASE_ASSERT(complete(), "Found that end_stream is not true");
}

// By default, automatically send a response when the request is complete.
void AutonomousStream::setEndStream(bool end_stream) {
  FakeStream::setEndStream(end_stream);
  if (end_stream) {
    sendResponse();
  }
}

// Reply with the canned response if the URI is /simple.txt. Otherwise this is a 503.
void AutonomousStream::sendResponse() {
  if (headers_->Path()->value() == "/simple.txt") {
    Envoy::Http::ResponseHeaderMapPtr response_headers{
        Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
            {{Envoy::Http::Headers::get().Status, "200"},
             {Http::LowerCaseString("Cache-Control"), "max-age=0"},
             {Http::LowerCaseString("Content-Type"), "text/plain"},
             {Http::LowerCaseString("X-Original-Url"),
              "https://test.example.com:6121/simple.txt"}})};
    encodeHeaders(*response_headers, /* headers_only_response= */ false);
    encodeData("This is a simple text file served by QUIC.\n", /* end_stream= */ true);
  }
}

AutonomousHttpConnection::AutonomousHttpConnection(AutonomousUpstream& autonomous_upstream,
                                                   SharedConnectionWrapper& shared_connection,
                                                   Http::CodecType type,
                                                   AutonomousUpstream& upstream)
    : FakeHttpConnection(autonomous_upstream, shared_connection, type, upstream.timeSystem(),
                         Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
                         envoy::config::core::v3::HttpProtocolOptions::ALLOW),
      upstream_(upstream) {}

Http::RequestDecoder& AutonomousHttpConnection::newStream(Http::ResponseEncoder& response_encoder,
                                                          bool) {
  auto stream = new AutonomousStream(*this, response_encoder, upstream_);
  streams_.push_back(FakeStreamPtr{stream});
  return *(stream);
}

AutonomousUpstream::~AutonomousUpstream() {
  // Make sure the dispatcher is stopped before the connections are destroyed.
  cleanUp();
  http_connections_.clear();
}

bool AutonomousUpstream::createNetworkFilterChain(Network::Connection& connection,
                                                  const std::vector<Network::FilterFactoryCb>&) {
  shared_connections_.emplace_back(new SharedConnectionWrapper(connection));
  AutonomousHttpConnectionPtr http_connection(
      new AutonomousHttpConnection(*this, *shared_connections_.back(), http_type_, *this));
  http_connection->initialize();
  http_connections_.push_back(std::move(http_connection));
  return true;
}

bool AutonomousUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void AutonomousUpstream::createUdpListenerFilterChain(Network::UdpListenerFilterManager&,
                                                      Network::UdpReadFilterCallbacks&) {}

} // namespace Envoy
