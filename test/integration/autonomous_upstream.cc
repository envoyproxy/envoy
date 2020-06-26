#include "test/integration/autonomous_upstream.h"

namespace Envoy {
namespace {

void HeaderToInt(const char header_name[], int32_t& return_int,
                 Http::TestResponseHeaderMapImpl& headers) {
  const std::string header_value(headers.get_(header_name));
  if (!header_value.empty()) {
    uint64_t parsed_value;
    RELEASE_ASSERT(absl::SimpleAtoi(header_value, &parsed_value) &&
                       parsed_value < static_cast<uint32_t>(std::numeric_limits<int32_t>::max()),
                   "");
    return_int = parsed_value;
  }
}

} // namespace

const char AutonomousStream::RESPONSE_SIZE_BYTES[] = "response_size_bytes";
const char AutonomousStream::EXPECT_REQUEST_SIZE_BYTES[] = "expect_request_size_bytes";
const char AutonomousStream::RESET_AFTER_REQUEST[] = "reset_after_request";

AutonomousStream::AutonomousStream(FakeHttpConnection& parent, Http::ResponseEncoder& encoder,
                                   AutonomousUpstream& upstream, bool allow_incomplete_streams)
    : FakeStream(parent, encoder, upstream.timeSystem()), upstream_(upstream),
      allow_incomplete_streams_(allow_incomplete_streams) {}

AutonomousStream::~AutonomousStream() {
  if (!allow_incomplete_streams_) {
    RELEASE_ASSERT(complete(), "Found that end_stream is not true");
  }
}

// By default, automatically send a response when the request is complete.
void AutonomousStream::setEndStream(bool end_stream) {
  FakeStream::setEndStream(end_stream);
  if (end_stream) {
    sendResponse();
  }
}

// Check all the special headers and send a customized response based on them.
void AutonomousStream::sendResponse() {
  Http::TestResponseHeaderMapImpl headers(*headers_);
  upstream_.setLastRequestHeaders(*headers_);

  int32_t request_body_length = -1;
  HeaderToInt(EXPECT_REQUEST_SIZE_BYTES, request_body_length, headers);
  if (request_body_length >= 0) {
    EXPECT_EQ(request_body_length, bodyLength());
  }

  if (!headers.get_(RESET_AFTER_REQUEST).empty()) {
    encodeResetStream();
    return;
  }

  int32_t response_body_length = 10;
  HeaderToInt(RESPONSE_SIZE_BYTES, response_body_length, headers);

  encodeHeaders(upstream_.responseHeaders(), false);
  encodeData(response_body_length, false);
  encodeTrailers(upstream_.responseTrailers());
}

AutonomousHttpConnection::AutonomousHttpConnection(AutonomousUpstream& autonomous_upstream,
                                                   SharedConnectionWrapper& shared_connection,
                                                   Type type, AutonomousUpstream& upstream)
    : FakeHttpConnection(autonomous_upstream, shared_connection, type, upstream.timeSystem(),
                         Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
                         envoy::config::core::v3::HttpProtocolOptions::ALLOW),
      upstream_(upstream) {}

Http::RequestDecoder& AutonomousHttpConnection::newStream(Http::ResponseEncoder& response_encoder,
                                                          bool) {
  auto stream =
      new AutonomousStream(*this, response_encoder, upstream_, upstream_.allow_incomplete_streams_);
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
  shared_connections_.emplace_back(new SharedConnectionWrapper(connection, true));
  AutonomousHttpConnectionPtr http_connection(
      new AutonomousHttpConnection(*this, *shared_connections_.back(), http_type_, *this));
  testing::AssertionResult result = http_connection->initialize();
  RELEASE_ASSERT(result, result.message());
  http_connections_.push_back(std::move(http_connection));
  return true;
}

bool AutonomousUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void AutonomousUpstream::createUdpListenerFilterChain(Network::UdpListenerFilterManager&,
                                                      Network::UdpReadFilterCallbacks&) {}

void AutonomousUpstream::setLastRequestHeaders(const Http::HeaderMap& headers) {
  Thread::LockGuard lock(headers_lock_);
  last_request_headers_ = std::make_unique<Http::TestRequestHeaderMapImpl>(headers);
}

std::unique_ptr<Http::TestRequestHeaderMapImpl> AutonomousUpstream::lastRequestHeaders() {
  Thread::LockGuard lock(headers_lock_);
  return std::move(last_request_headers_);
}

void AutonomousUpstream::setResponseTrailers(
    std::unique_ptr<Http::TestResponseTrailerMapImpl>&& response_trailers) {
  Thread::LockGuard lock(headers_lock_);
  response_trailers_ = std::move(response_trailers);
}

void AutonomousUpstream::setResponseHeaders(
    std::unique_ptr<Http::TestResponseHeaderMapImpl>&& response_headers) {
  Thread::LockGuard lock(headers_lock_);
  response_headers_ = std::move(response_headers);
}

Http::TestResponseTrailerMapImpl AutonomousUpstream::responseTrailers() {
  Thread::LockGuard lock(headers_lock_);
  Http::TestResponseTrailerMapImpl return_trailers = *response_trailers_;
  return return_trailers;
}

Http::TestResponseHeaderMapImpl AutonomousUpstream::responseHeaders() {
  Thread::LockGuard lock(headers_lock_);
  Http::TestResponseHeaderMapImpl return_headers = *response_headers_;
  return return_headers;
}

} // namespace Envoy
