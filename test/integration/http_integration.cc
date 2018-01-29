#include "test/integration/http_integration.h"

#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/utility.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::Invoke;
using testing::_;

namespace Envoy {

namespace {
std::string normalizeDate(const std::string& s) {
  const std::regex date_regex("date:[^\r]+");
  return std::regex_replace(s, date_regex, "date: Mon, 01 Jan 2017 00:00:00 GMT");
}

void setAllowAbsoluteUrl(envoy::api::v2::filter::network::HttpConnectionManager& hcm) {
  envoy::api::v2::Http1ProtocolOptions options;
  options.mutable_allow_absolute_url()->set_value(true);
  hcm.mutable_http_protocol_options()->CopyFrom(options);
};

envoy::api::v2::filter::network::HttpConnectionManager::CodecType
typeToCodecType(Http::CodecClient::Type type) {
  switch (type) {
  case Http::CodecClient::Type::HTTP1:
    return envoy::api::v2::filter::network::HttpConnectionManager::HTTP1;
  case Http::CodecClient::Type::HTTP2:
    return envoy::api::v2::filter::network::HttpConnectionManager::HTTP2;
  default:
    RELEASE_ASSERT(0);
  }
}

} // namespace

IntegrationCodecClient::IntegrationCodecClient(
    Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
    Upstream::HostDescriptionConstSharedPtr host_description, CodecClient::Type type)
    : CodecClientProd(type, std::move(conn), host_description), callbacks_(*this),
      codec_callbacks_(*this) {
  connection_->addConnectionCallbacks(callbacks_);
  setCodecConnectionCallbacks(codec_callbacks_);
  dispatcher.run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(connected_);
}

void IntegrationCodecClient::flushWrite() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  // NOTE: We should run blocking until all the body data is flushed.
}

void IntegrationCodecClient::makeHeaderOnlyRequest(const Http::HeaderMap& headers,
                                                   IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, true);
  flushWrite();
}

void IntegrationCodecClient::makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size,
                                                 IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, false);
  Buffer::OwnedImpl data(std::string(body_size, 'a'));
  encoder.encodeData(data, true);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, Buffer::Instance& data,
                                      bool end_stream) {
  encoder.encodeData(data, end_stream);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, uint64_t size,
                                      bool end_stream) {
  Buffer::OwnedImpl data(std::string(size, 'a'));
  sendData(encoder, data, end_stream);
}

void IntegrationCodecClient::sendTrailers(Http::StreamEncoder& encoder,
                                          const Http::HeaderMap& trailers) {
  encoder.encodeTrailers(trailers);
  flushWrite();
}

void IntegrationCodecClient::sendReset(Http::StreamEncoder& encoder) {
  encoder.getStream().resetStream(Http::StreamResetReason::LocalReset);
  flushWrite();
}

Http::StreamEncoder& IntegrationCodecClient::startRequest(const Http::HeaderMap& headers,
                                                          IntegrationStreamDecoder& response) {
  Http::StreamEncoder& encoder = newStream(response);
  encoder.getStream().addCallbacks(response);
  encoder.encodeHeaders(headers, false);
  flushWrite();
  return encoder;
}

void IntegrationCodecClient::waitForDisconnect() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);
  EXPECT_TRUE(disconnected_);
}

void IntegrationCodecClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    parent_.connected_ = true;
    parent_.connection_->dispatcher().exit();
  } else if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  }
}

IntegrationCodecClientPtr HttpIntegrationTest::makeHttpConnection(uint32_t port) {
  return makeHttpConnection(makeClientConnection(port));
}

IntegrationCodecClientPtr
HttpIntegrationTest::makeHttpConnection(Network::ClientConnectionPtr&& conn) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
      cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)))};
  return IntegrationCodecClientPtr{new IntegrationCodecClient(
      *dispatcher_, std::move(conn), host_description, downstream_protocol_)};
}

HttpIntegrationTest::HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                                         Network::Address::IpVersion version,
                                         const std::string& config)
    : BaseIntegrationTest(version, config), downstream_protocol_(downstream_protocol) {
  named_ports_ = {{"http"}};
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
}

HttpIntegrationTest::~HttpIntegrationTest() {
  cleanupUpstreamAndDownstream();
  test_server_.reset();
  fake_upstreams_.clear();
}

void HttpIntegrationTest::setDownstreamProtocol(Http::CodecClient::Type downstream_protocol) {
  downstream_protocol_ = downstream_protocol;
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
}

void HttpIntegrationTest::sendRequestAndWaitForResponse(Http::TestHeaderMapImpl& request_headers,
                                                        uint32_t request_body_size,
                                                        Http::TestHeaderMapImpl& response_headers,
                                                        uint32_t response_size) {
  // Send the request to Envoy.
  if (request_body_size) {
    codec_client_->makeRequestWithBody(request_headers, request_body_size, *response_);
  } else {
    codec_client_->makeHeaderOnlyRequest(request_headers, *response_);
  }
  waitForNextUpstreamRequest();
  // Send response headers, and end_stream if there is no respone body.
  upstream_request_->encodeHeaders(response_headers, response_size == 0);
  // Send any response data, with end_stream true.
  if (response_size) {
    upstream_request_->encodeData(response_size, true);
  }
  // Wait for the response to be read by the codec client.
  response_->waitForEndStream();
}

void HttpIntegrationTest::cleanupUpstreamAndDownstream() {
  if (codec_client_) {
    codec_client_->close();
  }
  if (fake_upstream_connection_) {
    fake_upstream_connection_->close();
    fake_upstream_connection_->waitForDisconnect();
  }
}

void HttpIntegrationTest::waitForNextUpstreamRequest(uint64_t upstream_index) {
  // If there is no upstream connection, wait for it to be established.
  if (!fake_upstream_connection_) {
    fake_upstream_connection_ =
        fake_upstreams_[upstream_index]->waitForHttpConnection(*dispatcher_);
  }
  // Wait for the next stream on the upstream connection.
  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  // Wait for the stream to be completely received.
  upstream_request_->waitForEndStream(*dispatcher_);
}

void HttpIntegrationTest::testRouterRequestAndResponseWithBody(
    uint64_t request_size, uint64_t response_size, bool big_header,
    ConnectionCreationFunction* create_connection) {
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  if (big_header) {
    request_headers.addCopy("big", std::string(4096, 'a'));
  }
  sendRequestAndWaitForResponse(request_headers, request_size, default_response_headers_,
                                response_size);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response_->body().size());
}

void HttpIntegrationTest::testRouterHeaderOnlyRequestAndResponse(
    bool close_upstream, ConnectionCreationFunction* create_connection) {
  // This is called multiple times per test in ads_integration_test. Only call
  // initialize() the first time.
  if (!initialized()) {
    initialize();
  }
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"x-lyft-user-id", "123"}};
  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // The following allows us to test shutting down the server with active connection pool
  // connections.
  if (!close_upstream) {
    test_server_.reset();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response_->body().size());
}

// Change the default route to be restrictive, and send a request to an alternate route.
void HttpIntegrationTest::testRouterNotFound() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

// Change the default route to be restrictive, and send a POST to an alternate route.
void HttpIntegrationTest::testRouterNotFoundWithBody() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/notfound", "foo", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

// Add a route that uses unknown cluster (expect 404 Not Found).
void HttpIntegrationTest::testRouterClusterNotFound404() {
  config_helper_.addRoute("foo.com", "/unknown", "unknown_cluster", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

// Add a route that uses unknown cluster (expect 503 Service Unavailable).
void HttpIntegrationTest::testRouterClusterNotFound503() {
  config_helper_.addRoute("foo.com", "/unknown", "unknown_cluster", false,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::NONE);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

// Add a route which redirects HTTP to HTTPS, and verify Envoy sends a 301
void HttpIntegrationTest::testRouterRedirect() {
  config_helper_.addRoute("www.redirect.com", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/foo", "", downstream_protocol_, version_, "www.redirect.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("301", response->headers().Status()->value().c_str());
  EXPECT_STREQ("https://www.redirect.com/foo",
               response->headers().get(Http::Headers::get().Location)->value().c_str());
}

void HttpIntegrationTest::testRouterDirectResponse() {
  const std::string body = "Response body";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", body);
  static const std::string domain("direct.example.com");
  static const std::string prefix("/");
  static const Http::Code status(Http::Code::OK);
  config_helper_.addConfigModifier(
      [&](envoy::api::v2::filter::network::HttpConnectionManager& hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->add_virtual_hosts();
        virtual_host->set_name(domain);
        virtual_host->add_domains(domain);
        virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
        virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
            static_cast<uint32_t>(status));
        virtual_host->mutable_routes(0)->mutable_direct_response()->mutable_body()->set_filename(
            file_path);
      });
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/", "", downstream_protocol_, version_, "direct.example.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(body, response->body());
}

// Add a health check filter and verify correct behavior when draining.
void HttpIntegrationTest::testDrainClose() {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();

  test_server_->drainManager().draining_ = true;
  codec_client_ = makeHttpConnection(lookupPort("http"));
  codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                               {":path", "/healthcheck"},
                                                               {":scheme", "http"},
                                                               {":authority", "host"}},
                                       *response_);
  response_->waitForEndStream();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }

  test_server_->drainManager().draining_ = false;
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeRequestComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                      {":path", "/test/long/url"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"}},
                              *response_);

  fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);

  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  upstream_request_->waitForHeadersComplete();
  fake_upstream_connection_->close();
  fake_upstream_connection_->waitForDisconnect();
  response_->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("503", response_->headers().Status()->value().c_str());
  EXPECT_EQ("upstream connect error or disconnect/reset before headers", response_->body());
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeResponseComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                               {":path", "/test/long/url"},
                                                               {":scheme", "http"},
                                                               {":authority", "host"}},
                                       *response_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  fake_upstream_connection_->close();
  fake_upstream_connection_->waitForDisconnect();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response_->waitForReset();
    codec_client_->close();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response_->body().size());
}

void HttpIntegrationTest::testRouterDownstreamDisconnectBeforeRequestComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();

  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                      {":path", "/test/long/url"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"}},
                              *response_);
  fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  upstream_request_->waitForHeadersComplete();
  codec_client_->close();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
  } else {
    upstream_request_->waitForReset();
    fake_upstream_connection_->close();
    fake_upstream_connection_->waitForDisconnect();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response_->complete());
}

void HttpIntegrationTest::testRouterDownstreamDisconnectBeforeResponseComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                               {":path", "/test/long/url"},
                                                               {":scheme", "http"},
                                                               {":authority", "host"}},
                                       *response_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, false);
  response_->waitForBodyData(512);
  codec_client_->close();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
  } else {
    upstream_request_->waitForReset();
    fake_upstream_connection_->close();
    fake_upstream_connection_->waitForDisconnect();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());
}

void HttpIntegrationTest::testRouterUpstreamResponseBeforeRequestComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                      {":path", "/test/long/url"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"}},
                              *response_);
  fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
  upstream_request_->waitForHeadersComplete();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response_->waitForEndStream();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
  } else {
    upstream_request_->waitForReset();
    fake_upstream_connection_->close();
    fake_upstream_connection_->waitForDisconnect();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());
}

void HttpIntegrationTest::testRetry() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"},
                                                             {"x-forwarded-for", "10.0.0.1"},
                                                             {"x-envoy-retry-on", "5xx"}},
                                     1024, *response_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
    fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  } else {
    upstream_request_->waitForReset();
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  response_->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());
}

void HttpIntegrationTest::testGrpcRetry() {
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"grpc-status", "0"}};
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  request_encoder_ =
      &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"x-forwarded-for", "10.0.0.1"},
                                                           {"x-envoy-retry-grpc-on", "cancelled"}},
                                   *response_);
  codec_client_->sendData(*request_encoder_, 1024, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}}, false);
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
    fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  } else {
    upstream_request_->waitForReset();
  }
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512,
                                fake_upstreams_[0]->httpType() != FakeHttpConnection::Type::HTTP2);
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    upstream_request_->encodeTrailers(response_trailers);
  }

  response_->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response_->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}

// Very similar set-up to testRetry but with a 16k request the request will not
// be buffered and the 503 will be returned to the user.
void HttpIntegrationTest::testRetryHittingBufferLimit() {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"},
                                                             {"x-forwarded-for", "10.0.0.1"},
                                                             {"x-envoy-retry-on", "5xx"}},
                                     1024 * 65, *response_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);

  response_->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(66560U, upstream_request_->bodyLength());

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("503", response_->headers().Status()->value().c_str());
}

// Test hitting the dynamo filter with too many request bytes to buffer. Ensure the connection
// manager sends a 413.
void HttpIntegrationTest::testHittingDecoderFilterLimit() {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Envoy will likely connect and proxy some unspecified amount of data before
  // hitting the buffer limit and disconnecting. Ignore this if it happens.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                             {":path", "/dynamo/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"},
                                                             {"x-forwarded-for", "10.0.0.1"},
                                                             {"x-envoy-retry-on", "5xx"}},
                                     1024 * 65, *response_);

  response_->waitForEndStream();
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("413", response_->headers().Status()->value().c_str());
}

// Test hitting the dynamo filter with too many response bytes to buffer. Given the request headers
// are sent on early, the stream/connection will be reset.
void HttpIntegrationTest::testHittingEncoderFilterLimit() {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto downstream_request =
      &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/dynamo/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response_);
  Buffer::OwnedImpl data("{\"TableName\":\"locations\"}");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the respone headers.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  // Now send an overly large response body.
  upstream_request_->encodeData(1024 * 65, false);

  response_->waitForEndStream();
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("500", response_->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testTwoRequests() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"}},
                                     1024, *response_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response_->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());

  // Request 2.
  response_.reset(new IntegrationStreamDecoder(*dispatcher_));
  codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"}},
                                     512, *response_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024, true);
  response_->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(512U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(1024U, response_->body().size());
}

void HttpIntegrationTest::testBadFirstline() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse("hello", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testMissingDelimiter() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse("GET / HTTP/1.1\r\nHost: host\r\nfoo bar\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testInvalidCharacterInFirstline() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse("GE(T / HTTP/1.1\r\nHost: host\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testLowVersion() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse("GET / HTTP/0.8\r\nHost: host\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testHttp10Request() {
  initialize();
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

void HttpIntegrationTest::testNoHost() {
  initialize();
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
}

void HttpIntegrationTest::testAbsolutePath() {
  // Configure www.redirect.com to send a redirect, and ensure the redirect is
  // encountered via absolute URL.
  config_helper_.addRoute("www.redirect.com", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);

  initialize();
  Buffer::OwnedImpl buffer("GET http://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_FALSE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void HttpIntegrationTest::testAbsolutePathWithPort() {
  // Configure www.namewithport.com:1234 to send a redirect, and ensure the redirect is
  // encountered via absolute URL with a port.
  config_helper_.addRoute("www.namewithport.com:1234", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);
  initialize();
  Buffer::OwnedImpl buffer("GET http://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_FALSE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void HttpIntegrationTest::testAbsolutePathWithoutPort() {
  // Add a restrictive default match, to avoid the request hitting the * / catchall.
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  // Set a matcher for namewithport:1234 and verify http://namewithport does not match
  config_helper_.addRoute("www.namewithport.com:1234", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);
  initialize();
  Buffer::OwnedImpl buffer("GET http://www.namewithport.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void HttpIntegrationTest::testAllowAbsoluteSameRelative() {
  // TODO(mattwoodyard) run this test.
  // Ensure that relative urls behave the same with allow_absolute_url enabled and without
  testEquivalent("GET /foo/bar HTTP/1.1\r\nHost: host\r\n\r\n");
}

void HttpIntegrationTest::testConnect() {
  // Ensure that connect behaves the same with allow_absolute_url enabled and without
  testEquivalent("CONNECT www.somewhere.com:80 HTTP/1.1\r\nHost: host\r\n\r\n");
}

void HttpIntegrationTest::testEquivalent(const std::string& request) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    // Clone the whole listener.
    auto static_resources = bootstrap.mutable_static_resources();
    auto* old_listener = static_resources->mutable_listeners(0);
    auto* cloned_listener = static_resources->add_listeners();
    cloned_listener->CopyFrom(*old_listener);
    cloned_listener->set_name("listener2");
  });
  // Set the first listener to allow absoute URLs.
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);
  // Make sure both listeners can be reached.
  // TODO(alyssar) in a follow-up, instead have these named ports pulled automatically from the
  // listener names.
  named_ports_ = {"http_forward", "http"};
  initialize();

  Buffer::OwnedImpl buffer1(request);
  std::string response1;
  RawConnectionDriver connection1(
      lookupPort("http"), buffer1,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response1.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection1.run();

  Buffer::OwnedImpl buffer2(request);
  std::string response2;
  RawConnectionDriver connection2(
      lookupPort("http_forward"), buffer2,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response2.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection2.run();

  EXPECT_EQ(normalizeDate(response1), normalizeDate(response2));
}

void HttpIntegrationTest::testBadPath() {
  initialize();
  Buffer::OwnedImpl buffer("GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"), buffer,
      [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        client.close(Network::ConnectionCloseType::NoFlush);
      },
      version_);

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void HttpIntegrationTest::testValidZeroLengthContent() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"content-length", "0"}};
  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testInvalidContentLength() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/test/long/url"},
                                                      {":authority", "host"},
                                                      {"content-length", "-1"}},
                              *response_);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response_->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response_->complete());
    EXPECT_STREQ("400", response_->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response_->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response_->reset_reason());
  }
}

void HttpIntegrationTest::testMultipleContentLengths() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/test/long/url"},
                                                      {":authority", "host"},
                                                      {"content-length", "3,2"}},
                              *response_);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response_->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response_->complete());
    EXPECT_STREQ("400", response_->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response_->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response_->reset_reason());
  }
}

void HttpIntegrationTest::testOverlyLongHeaders() {
  Http::TestHeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addCopy("big", std::string(60 * 1024, 'a'));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string long_value(7500, 'x');
  codec_client_->startRequest(big_headers, *response_);

  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("431", response_->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testUpstreamProtocolError() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                      {":path", "/test/long/url"},
                                                      {":authority", "host"}},
                              *response_);

  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  // TODO(mattklein123): Waiting for exact amount of data is a hack. This needs to
  // be fixed.
  fake_upstream_connection->waitForData(187);
  fake_upstream_connection->write("bad protocol data!");
  fake_upstream_connection->waitForDisconnect();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("503", response_->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testDownstreamResetBeforeResponseComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  request_encoder_ =
      &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"cookie", "a=b"},
                                                           {"cookie", "c=d"}},
                                   *response_);
  codec_client_->sendData(*request_encoder_, 0, true);
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Cookie)->value(), "a=b; c=d");

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, false);

  response_->waitForBodyData(512);
  codec_client_->sendReset(*request_encoder_);

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    fake_upstream_connection_->waitForDisconnect();
  } else {
    upstream_request_->waitForReset();
    fake_upstream_connection_->close();
    fake_upstream_connection_->waitForDisconnect();
  }

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response_->body().size());
}

void HttpIntegrationTest::testTrailers(uint64_t request_size, uint64_t response_size) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  Http::TestHeaderMapImpl request_trailers{{"request1", "trailer1"}, {"request2", "trailer2"}};
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"response2", "trailer2"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  request_encoder_ =
      &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response_);
  codec_client_->sendData(*request_encoder_, request_size, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(response_size, false);
  upstream_request_->encodeTrailers(response_trailers);
  response_->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));
  }

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response_->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response_->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}
} // namespace Envoy
