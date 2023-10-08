#include "test/integration/autonomous_upstream.h"

namespace Envoy {
namespace {

void headerToInt(const char header_name[], int32_t& return_int,
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
const char AutonomousStream::RESPONSE_DATA_BLOCKS[] = "response_data_blocks";
const char AutonomousStream::EXPECT_REQUEST_SIZE_BYTES[] = "expect_request_size_bytes";
const char AutonomousStream::RESET_AFTER_REQUEST[] = "reset_after_request";
const char AutonomousStream::CLOSE_AFTER_RESPONSE[] = "close_after_response";
const char AutonomousStream::NO_TRAILERS[] = "no_trailers";
const char AutonomousStream::NO_END_STREAM[] = "no_end_stream";

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
  headerToInt(EXPECT_REQUEST_SIZE_BYTES, request_body_length, headers);
  if (request_body_length >= 0) {
    EXPECT_EQ(request_body_length, body_.length());
  }

  if (!headers.get_(RESET_AFTER_REQUEST).empty()) {
    encodeResetStream();
    return;
  }

  int32_t response_body_length = 10;
  headerToInt(RESPONSE_SIZE_BYTES, response_body_length, headers);

  int32_t response_data_blocks = 1;
  headerToInt(RESPONSE_DATA_BLOCKS, response_data_blocks, headers);

  const bool end_stream = headers.get_(NO_END_STREAM).empty();
  const bool send_trailers = end_stream && headers.get_(NO_TRAILERS).empty();
  const bool headers_only_response = !send_trailers && response_data_blocks == 0 && end_stream;

  pre_response_headers_metadata_ = upstream_.preResponseHeadersMetadata();
  if (pre_response_headers_metadata_) {
    encodeMetadata(*pre_response_headers_metadata_);
  }

  encodeHeaders(upstream_.responseHeaders(), headers_only_response);
  if (!headers_only_response) {
    if (upstream_.responseBody().has_value()) {
      encodeData(*upstream_.responseBody(), !send_trailers);
    } else {
      for (int32_t i = 0; i < response_data_blocks; ++i) {
        encodeData(response_body_length,
                   i == (response_data_blocks - 1) && !send_trailers && end_stream);
      }
    }
    if (send_trailers) {
      encodeTrailers(upstream_.responseTrailers());
    }
  }
  if (!headers.get_(CLOSE_AFTER_RESPONSE).empty()) {
    parent_.connection().dispatcher().post(
        [this]() -> void { parent_.connection().close(Network::ConnectionCloseType::FlushWrite); });
    return;
  }
}

AutonomousHttpConnection::AutonomousHttpConnection(AutonomousUpstream& autonomous_upstream,
                                                   SharedConnectionWrapper& shared_connection,
                                                   Http::CodecType type,
                                                   uint32_t max_request_headers_kb,
                                                   uint32_t max_request_headers_count,
                                                   AutonomousUpstream& upstream)
    : FakeHttpConnection(autonomous_upstream, shared_connection, type, upstream.timeSystem(),
                         max_request_headers_kb, max_request_headers_count,
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
                                                  const Filter::NetworkFilterFactoriesList&) {
  shared_connections_.emplace_back(new SharedConnectionWrapper(connection));
  AutonomousHttpConnectionPtr http_connection(new AutonomousHttpConnection(
      *this, *shared_connections_.back(), http_type_, config().max_request_headers_kb_,
      config().max_request_headers_count_, *this));
  http_connection->initialize();
  http_connections_.push_back(std::move(http_connection));
  return true;
}

bool AutonomousUpstream::createListenerFilterChain(Network::ListenerFilterManager&) { return true; }

void AutonomousUpstream::createUdpListenerFilterChain(Network::UdpListenerFilterManager&,
                                                      Network::UdpReadFilterCallbacks&) {}

bool AutonomousUpstream::createQuicListenerFilterChain(Network::QuicListenerFilterManager&) {
  return true;
}

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

void AutonomousUpstream::setResponseBody(std::string body) {
  Thread::LockGuard lock(headers_lock_);
  response_body_ = body;
}

void AutonomousUpstream::setResponseHeaders(
    std::unique_ptr<Http::TestResponseHeaderMapImpl>&& response_headers) {
  Thread::LockGuard lock(headers_lock_);
  response_headers_ = std::move(response_headers);
}

void AutonomousUpstream::setPreResponseHeadersMetadata(
    std::unique_ptr<Http::MetadataMapVector>&& metadata) {
  Thread::LockGuard lock(headers_lock_);
  pre_response_headers_metadata_ = std::move(metadata);
}

Http::TestResponseTrailerMapImpl AutonomousUpstream::responseTrailers() {
  Thread::LockGuard lock(headers_lock_);
  Http::TestResponseTrailerMapImpl return_trailers = *response_trailers_;
  return return_trailers;
}

absl::optional<std::string> AutonomousUpstream::responseBody() {
  Thread::LockGuard lock(headers_lock_);
  return response_body_;
}

Http::TestResponseHeaderMapImpl AutonomousUpstream::responseHeaders() {
  Thread::LockGuard lock(headers_lock_);
  Http::TestResponseHeaderMapImpl return_headers = *response_headers_;
  return return_headers;
}

std::unique_ptr<Http::MetadataMapVector> AutonomousUpstream::preResponseHeadersMetadata() {
  Thread::LockGuard lock(headers_lock_);
  return std::move(pre_response_headers_metadata_);
}

AssertionResult AutonomousUpstream::closeConnection(uint32_t index,
                                                    std::chrono::milliseconds timeout) {
  return shared_connections_[index]->executeOnDispatcher(
      [](Network::Connection& connection) {
        ASSERT(connection.state() == Network::Connection::State::Open);
        connection.close(Network::ConnectionCloseType::FlushWrite);
      },
      timeout);
}

} // namespace Envoy
