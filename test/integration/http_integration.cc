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
#include "common/http/headers.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/test_host_predicate_config.h"
#include "test/integration/utility.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::HasSubstr;
using testing::Invoke;
using testing::Not;

namespace Envoy {

namespace {
std::string normalizeDate(const std::string& s) {
  const std::regex date_regex("date:[^\r]+");
  return std::regex_replace(s, date_regex, "date: Mon, 01 Jan 2017 00:00:00 GMT");
}

void setAllowAbsoluteUrl(
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
  envoy::api::v2::core::Http1ProtocolOptions options;
  options.mutable_allow_absolute_url()->set_value(true);
  hcm.mutable_http_protocol_options()->CopyFrom(options);
};

void setAllowHttp10WithDefaultHost(
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
  hcm.mutable_http_protocol_options()->set_accept_http_10(true);
  hcm.mutable_http_protocol_options()->set_default_host_for_http_10("default.com");
}

envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::CodecType
typeToCodecType(Http::CodecClient::Type type) {
  switch (type) {
  case Http::CodecClient::Type::HTTP1:
    return envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
        HTTP1;
  case Http::CodecClient::Type::HTTP2:
    return envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
        HTTP2;
  default:
    RELEASE_ASSERT(0, "");
  }
}

} // namespace

IntegrationCodecClient::IntegrationCodecClient(
    Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
    Upstream::HostDescriptionConstSharedPtr host_description, CodecClient::Type type)
    : CodecClientProd(type, std::move(conn), host_description, dispatcher), dispatcher_(dispatcher),
      callbacks_(*this), codec_callbacks_(*this) {
  connection_->addConnectionCallbacks(callbacks_);
  setCodecConnectionCallbacks(codec_callbacks_);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

void IntegrationCodecClient::flushWrite() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  // NOTE: We should run blocking until all the body data is flushed.
}

IntegrationStreamDecoderPtr
IntegrationCodecClient::makeHeaderOnlyRequest(const Http::HeaderMap& headers) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::StreamEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, true);
  flushWrite();
  return response;
}

IntegrationStreamDecoderPtr
IntegrationCodecClient::makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::StreamEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, false);
  Buffer::OwnedImpl data(std::string(body_size, 'a'));
  encoder.encodeData(data, true);
  flushWrite();
  return response;
}

void IntegrationCodecClient::sendData(Http::StreamEncoder& encoder, absl::string_view data,
                                      bool end_stream) {
  Buffer::OwnedImpl buffer_data(data.data(), data.size());
  encoder.encodeData(buffer_data, end_stream);
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

std::pair<Http::StreamEncoder&, IntegrationStreamDecoderPtr>
IntegrationCodecClient::startRequest(const Http::HeaderMap& headers) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::StreamEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, false);
  flushWrite();
  return {encoder, std::move(response)};
}

bool IntegrationCodecClient::waitForDisconnect(std::chrono::milliseconds time_to_wait) {
  Event::TimerPtr wait_timer;
  bool wait_timer_triggered = false;
  if (time_to_wait.count()) {
    wait_timer = connection_->dispatcher().createTimer([this, &wait_timer_triggered] {
      connection_->dispatcher().exit();
      wait_timer_triggered = true;
    });
    wait_timer->enableTimer(time_to_wait);
  }

  connection_->dispatcher().run(Event::Dispatcher::RunType::Block);

  // Disable the timer if it was created. This call is harmless if the timer already triggered.
  if (wait_timer) {
    wait_timer->disableTimer();
  }

  if (wait_timer_triggered && !disconnected_) {
    return false;
  }
  EXPECT_TRUE(disconnected_);

  return true;
}

void IntegrationCodecClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  parent_.last_connection_event_ = event;
  if (event == Network::ConnectionEvent::Connected) {
    parent_.connected_ = true;
    parent_.connection_->dispatcher().exit();
  } else if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  } else {
    parent_.disconnected_ = true;
  }
}

IntegrationCodecClientPtr HttpIntegrationTest::makeHttpConnection(uint32_t port) {
  return makeHttpConnection(makeClientConnection(port));
}

IntegrationCodecClientPtr
HttpIntegrationTest::makeRawHttpConnection(Network::ClientConnectionPtr&& conn) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  cluster->http2_settings_.allow_connect_ = true;
  Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
      cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)))};
  return IntegrationCodecClientPtr{new IntegrationCodecClient(
      *dispatcher_, std::move(conn), host_description, downstream_protocol_)};
}

IntegrationCodecClientPtr
HttpIntegrationTest::makeHttpConnection(Network::ClientConnectionPtr&& conn) {
  auto codec = makeRawHttpConnection(std::move(conn));
  EXPECT_TRUE(codec->connected());
  return codec;
}

HttpIntegrationTest::HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                                         Network::Address::IpVersion version,
                                         TestTimeSystemPtr time_system, const std::string& config)
    : BaseIntegrationTest(version, std::move(time_system), config),
      downstream_protocol_(downstream_protocol) {
  // Legacy integration tests expect the default listener to be named "http" for lookupPort calls.
  config_helper_.renameListener("http");
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
}

HttpIntegrationTest::~HttpIntegrationTest() {
  cleanupUpstreamAndDownstream();
  test_server_.reset();
  fake_upstream_connection_.reset();
  fake_upstreams_.clear();
}

void HttpIntegrationTest::setDownstreamProtocol(Http::CodecClient::Type downstream_protocol) {
  downstream_protocol_ = downstream_protocol;
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
}

IntegrationStreamDecoderPtr HttpIntegrationTest::sendRequestAndWaitForResponse(
    const Http::TestHeaderMapImpl& request_headers, uint32_t request_body_size,
    const Http::TestHeaderMapImpl& response_headers, uint32_t response_size) {
  ASSERT(codec_client_ != nullptr);
  // Send the request to Envoy.
  IntegrationStreamDecoderPtr response;
  if (request_body_size) {
    response = codec_client_->makeRequestWithBody(request_headers, request_body_size);
  } else {
    response = codec_client_->makeHeaderOnlyRequest(request_headers);
  }
  waitForNextUpstreamRequest();
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(response_headers, response_size == 0);
  // Send any response data, with end_stream true.
  if (response_size) {
    upstream_request_->encodeData(response_size, true);
  }
  // Wait for the response to be read by the codec client.
  response->waitForEndStream();
  return response;
}

void HttpIntegrationTest::cleanupUpstreamAndDownstream() {
  // Close the upstream connection first. If there's an outstanding request,
  // closing the client may result in a FIN being sent upstream, and FakeConnectionBase::close
  // will interpret that as an unexpected disconnect. The codec client is not
  // subject to the same failure mode.
  if (fake_upstream_connection_) {
    AssertionResult result = fake_upstream_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
  if (codec_client_) {
    codec_client_->close();
  }
}

uint64_t
HttpIntegrationTest::waitForNextUpstreamRequest(const std::vector<uint64_t>& upstream_indices) {
  uint64_t upstream_with_request;
  // If there is no upstream connection, wait for it to be established.
  if (!fake_upstream_connection_) {
    AssertionResult result = AssertionFailure();
    for (auto upstream_index : upstream_indices) {
      result = fake_upstreams_[upstream_index]->waitForHttpConnection(*dispatcher_,
                                                                      fake_upstream_connection_);
      if (result) {
        upstream_with_request = upstream_index;
        break;
      }
    }
    RELEASE_ASSERT(result, result.message());
  }
  // Wait for the next stream on the upstream connection.
  AssertionResult result =
      fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  // Wait for the stream to be completely received.
  result = upstream_request_->waitForEndStream(*dispatcher_);
  RELEASE_ASSERT(result, result.message());

  return upstream_with_request;
}

void HttpIntegrationTest::waitForNextUpstreamRequest(uint64_t upstream_index) {
  waitForNextUpstreamRequest(std::vector<uint64_t>({upstream_index}));
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
  auto response = sendRequestAndWaitForResponse(request_headers, request_size,
                                                default_response_headers_, response_size);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());
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
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // The following allows us to test shutting down the server with active connection pool
  // connections.
  if (!close_upstream) {
    test_server_.reset();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

// Change the default route to be restrictive, and send a request to an alternate route.
void HttpIntegrationTest::testRouterNotFound() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

// Change the default route to be restrictive, and send a POST to an alternate route.
void HttpIntegrationTest::testRouterNotFoundWithBody() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/notfound", "foo", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
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
  ASSERT_TRUE(response->complete());
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
  ASSERT_TRUE(response->complete());
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
  ASSERT_TRUE(response->complete());
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
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("x-additional-header");
        header_value_option->mutable_header()->set_value("example-value");
        header_value_option->mutable_append()->set_value(false);
        header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_key("content-type");
        header_value_option->mutable_header()->set_value("text/html");
        header_value_option->mutable_append()->set_value(false);
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
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("example-value", response->headers()
                                    .get(Envoy::Http::LowerCaseString("x-additional-header"))
                                    ->value()
                                    .c_str());
  EXPECT_STREQ("text/html", response->headers().ContentType()->value().c_str());
  EXPECT_EQ(body, response->body());
}

// Add a health check filter and verify correct computation of health based on upstream status.
void HttpIntegrationTest::testComputedHealthCheck() {
  config_helper_.addFilter(R"EOF(
name: envoy.health_check
config:
    pass_through_mode: false
    cluster_min_healthy_percentages:
        example_cluster_name: { value: 75 }
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testAddEncodedTrailers() {
  config_helper_.addFilter(R"EOF(
name: add-trailers-filter
config: {}
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  response->waitForEndStream();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_STREQ("decode", upstream_request_->trailers()->GrpcMessage()->value().c_str());
  }
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_STREQ("encode", response->trailers()->GrpcMessage()->value().c_str());
  }
}

// Add a health check filter and verify correct behavior when draining.
void HttpIntegrationTest::testDrainClose() {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();

  test_server_->drainManager().draining_ = true;
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  response->waitForEndStream();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }

  test_server_->drainManager().draining_ = false;
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeRequestComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  response->waitForEndStream();

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ("upstream connect error or disconnect/reset before headers", response->body());
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeResponseComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response->waitForReset();
    codec_client_->close();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

void HttpIntegrationTest::testRouterDownstreamDisconnectBeforeRequestComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();

  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->close();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
}

void HttpIntegrationTest::testRouterDownstreamDisconnectBeforeResponseComplete(
    ConnectionCreationFunction* create_connection) {
#ifdef __APPLE__
  // Skip this test on OS X: we can't detect the early close on OS X, and we
  // won't clean up the upstream connection until it times out. See #4294.
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    return;
  }
#endif
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto response =
      codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, false);
  response->waitForBodyData(512);
  codec_client_->close();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testRouterUpstreamResponseBeforeRequestComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testRetry() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

// Change the default route to be restrictive, and send a request to an alternate route.
void HttpIntegrationTest::testGrpcRouterNotFound() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/service/notfound", "", downstream_protocol_, version_, "host",
      Http::Headers::get().ContentTypeValues.Grpc);
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            response->headers().ContentType()->value().c_str());
  EXPECT_STREQ("12", response->headers().GrpcStatus()->value().c_str());
}

void HttpIntegrationTest::testGrpcRetry() {
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"grpc-status", "0"}};
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"x-forwarded-for", "10.0.0.1"},
                                                          {"x-envoy-retry-grpc-on", "cancelled"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1024, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}}, false);
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512,
                                fake_upstreams_[0]->httpType() != FakeHttpConnection::Type::HTTP2);
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    upstream_request_->encodeTrailers(response_trailers);
  }

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}

// Verifies that a retry priority can be configured and affect the host selected during retries.
// The retry priority will always target P1, which would otherwise never be hit due to P0 being
// healthy.
void HttpIntegrationTest::testRetryPriority() {
  const Upstream::PriorityLoad priority_load{0, 100};
  Upstream::MockRetryPriorityFactory factory(
      std::make_shared<NiceMock<Upstream::MockRetryPriority>>(priority_load));

  Registry::InjectFactory<Upstream::RetryPriorityFactory> inject_factory(factory);

  envoy::api::v2::route::RouteAction::RetryPolicy retry_policy;
  retry_policy.mutable_retry_priority()->set_name(factory.name());

  // Add route with custom retry policy
  config_helper_.addRoute("host", "/test_retry", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, retry_policy);

  // Use load assignments instead of static hosts. Necessary in order to use priorities.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto load_assignment = cluster->mutable_load_assignment();
    load_assignment->set_cluster_name(cluster->name());
    const auto& host_address = cluster->hosts(0).socket_address().address();

    for (int i = 0; i < 2; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->set_priority(i);
      locality->mutable_locality()->set_region("region");
      locality->mutable_locality()->set_zone("zone");
      locality->mutable_locality()->set_sub_zone("sub_zone" + std::to_string(i));
      auto lb_endpoint = locality->add_lb_endpoints();
      lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
          host_address);
      lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
          0);
    }

    cluster->clear_hosts();
  });

  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test_retry"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);

  // Note how we're exepcting each upstream request to hit the same upstream.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

//
// Verifies that a retry host filter can be configured and affect the host selected during retries.
// The predicate will keep track of the first host attempted, and attempt to route all requests to
// the same host. With a total of two upstream hosts, this should result in us continuously sending
// requests to the same host.
void HttpIntegrationTest::testRetryHostPredicateFilter() {
  TestHostPredicateFactory predicate_factory;
  Registry::InjectFactory<Upstream::RetryHostPredicateFactory> inject_factory(predicate_factory);

  envoy::api::v2::route::RouteAction::RetryPolicy retry_policy;
  retry_policy.add_retry_host_predicate()->set_name(predicate_factory.name());

  // Add route with custom retry policy
  config_helper_.addRoute("host", "/test_retry", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, retry_policy);

  // We want to work with a cluster with two hosts.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* new_host = bootstrap.mutable_static_resources()->mutable_clusters(0)->add_hosts();
    new_host->MergeFrom(bootstrap.static_resources().clusters(0).hosts(0));
  });
  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test_retry"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);

  // Note how we're exepcting each upstream request to hit the same upstream.
  auto upstream_idx = waitForNextUpstreamRequest({0, 1});
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[upstream_idx]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[upstream_idx]->waitForHttpConnection(*dispatcher_,
                                                                     fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest(upstream_idx);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

// Very similar set-up to testRetry but with a 16k request the request will not
// be buffered and the 503 will be returned to the user.
void HttpIntegrationTest::testRetryHittingBufferLimit() {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024 * 65);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(66560U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
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
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024 * 65);

  response->waitForEndStream();
  // With HTTP/1 there's a possible race where if the connection backs up early,
  // the 413-and-connection-close may be sent while the body is still being
  // sent, resulting in a write error and the connection being closed before the
  // response is read.
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    ASSERT_TRUE(response->complete());
  }
  if (response->complete()) {
    EXPECT_STREQ("413", response->headers().Status()->value().c_str());
  }
}

// Test hitting the dynamo filter with too many response bytes to buffer. Given the request headers
// are sent on early, the stream/connection will be reset.
void HttpIntegrationTest::testHittingEncoderFilterLimit() {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/dynamo/url"}, {":scheme", "http"}, {":authority", "host"}});
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("{\"TableName\":\"locations\"}");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("500", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testEnvoyHandling100Continue(bool additional_continue_from_upstream,
                                                       const std::string& via) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/dynamo/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"expect", "100-continue"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  // The continue headers should arrive immediately.
  response->waitForContinueHeaders();
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send the rest of the request.
  codec_client_->sendData(*request_encoder_, 10, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Verify the Expect header is stripped.
  EXPECT_EQ(nullptr, upstream_request_->headers().get(Http::Headers::get().Expect));
  if (via.empty()) {
    EXPECT_EQ(nullptr, upstream_request_->headers().get(Http::Headers::get().Via));
  } else {
    EXPECT_STREQ(via.c_str(),
                 upstream_request_->headers().get(Http::Headers::get().Via)->value().c_str());
  }

  if (additional_continue_from_upstream) {
    // Make sure if upstream sends an 100-Continue Envoy doesn't send its own and proxy the one
    // from upstream!
    upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});
  }
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(12, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  ASSERT(response->continue_headers() != nullptr);
  EXPECT_STREQ("100", response->continue_headers()->Status()->value().c_str());
  EXPECT_EQ(nullptr, response->continue_headers()->Via());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  if (via.empty()) {
    EXPECT_EQ(nullptr, response->headers().Via());
  } else {
    EXPECT_STREQ(via.c_str(), response->headers().Via()->value().c_str());
  }
}

void HttpIntegrationTest::testEnvoyProxying100Continue(bool continue_before_upstream_complete,
                                                       bool with_encoder_filter) {
  if (with_encoder_filter) {
    // Because 100-continue only affects encoder filters, make sure it plays well with one.
    config_helper_.addFilter("name: envoy.cors");
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          {
            auto* cors = virtual_host->mutable_cors();
            cors->add_allow_origin("*");
            cors->set_allow_headers("content-type,x-grpc-web");
            cors->set_allow_methods("GET,POST");
          }
        });
  }
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void { hcm.set_proxy_100_continue(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/dynamo/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"expect", "100-continue"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  if (continue_before_upstream_complete) {
    // This case tests sending on 100-Continue headers before the client has sent all the
    // request data.
    upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});
    response->waitForContinueHeaders();
  }
  // Send all of the request data and wait for it to be received upstream.
  codec_client_->sendData(*request_encoder_, 10, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  if (!continue_before_upstream_complete) {
    // This case tests forwarding 100-Continue after the client has sent all data.
    upstream_request_->encode100ContinueHeaders(Http::TestHeaderMapImpl{{":status", "100"}});
    response->waitForContinueHeaders();
  }
  // Now send the rest of the response.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  ASSERT(response->continue_headers() != nullptr);
  EXPECT_STREQ("100", response->continue_headers()->Status()->value().c_str());

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testIdleTimeoutBasic() {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* http_protocol_options = cluster->mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Do not send any requests and validate if idle time out kicks in.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 1);
}

void HttpIntegrationTest::testIdleTimeoutWithTwoRequests() {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* http_protocol_options = cluster->mutable_common_http_protocol_options();
    auto* idle_time_out = http_protocol_options->mutable_idle_timeout();
    std::chrono::milliseconds timeout(1000);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    idle_time_out->set_seconds(seconds.count());
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Request 2.
  response = codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                512);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);

  // Do not send any requests and validate if idle time out kicks in.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_idle_timeout", 1);
}

void HttpIntegrationTest::testUpstreamDisconnectWithTwoRequests() {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         1024);
  waitForNextUpstreamRequest();

  // Request 2.
  IntegrationCodecClientPtr codec_client2 = makeHttpConnection(lookupPort("http"));
  auto response2 =
      codec_client2->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         512);

  // Response 1.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  // Response 2.
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024, true);
  response2->waitForEndStream();
  codec_client2->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response2->complete());
  EXPECT_STREQ("200", response2->headers().Status()->value().c_str());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 2);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 2);
}

void HttpIntegrationTest::testTwoRequests() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());

  // Request 2.
  response = codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                512);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024, true);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(512U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(1024U, response->body().size());
}

void HttpIntegrationTest::testBadFirstline() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "hello", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testMissingDelimiter() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\nHost: host\r\nfoo bar\r\n\r\n", &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testInvalidCharacterInFirstline() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GE(T / HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testInvalidVersion() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.01\r\nHost: host\r\n\r\n",
                                &response);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

void HttpIntegrationTest::testHttp10Disabled() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

// Turn HTTP/1.0 support on and verify the request is proxied and the default host is sent upstream.
void HttpIntegrationTest::testHttp10Enabled() {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers.get() != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");

  sendRawHttpAndWaitForResponse(lookupPort("http"), "HEAD / HTTP/1.0\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));
}

// Verify for HTTP/1.0 a keep-alive header results in no connection: close.
// Also verify existing host headers are passed through for the HTTP/1.0 case.
void HttpIntegrationTest::testHttp10WithHostAndKeepAlive() {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.0\r\nHost: foo.com\r\nConnection:Keep-alive\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, Not(HasSubstr("connection: close")));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers.get() != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
}

// Turn HTTP/1.0 support on and verify 09 style requests work.
void HttpIntegrationTest::testHttp09Enabled() {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"), "GET /\r\n\r\n", &response, false);
  EXPECT_THAT(response, HasSubstr("HTTP/1.0 200 OK\r\n"));
  EXPECT_THAT(response, HasSubstr("connection: close"));
  EXPECT_THAT(response, Not(HasSubstr("transfer-encoding: chunked\r\n")));

  std::unique_ptr<Http::TestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers.get() != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "default.com");
}

void HttpIntegrationTest::testAbsolutePath() {
  // Configure www.redirect.com to send a redirect, and ensure the redirect is
  // encountered via absolute URL.
  config_helper_.addRoute("www.redirect.com", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);

  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.redirect.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
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
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET http://www.namewithport.com:1234 HTTP/1.1\r\nHost: host\r\n\r\n",
      &response, true);
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
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://www.namewithport.com HTTP/1.1\r\nHost: host\r\n\r\n",
                                &response, true);
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0) << response;
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

void HttpIntegrationTest::testInlineHeaders() {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(&setAllowHttp10WithDefaultHost);
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET / HTTP/1.1\r\n"
                                "Host: foo.com\r\n"
                                "Foo: bar\r\n"
                                "Cache-control: public\r\n"
                                "Cache-control: 123\r\n"
                                "Eep: baz\r\n\r\n",
                                &response, true);
  EXPECT_THAT(response, HasSubstr("HTTP/1.1 200 OK\r\n"));

  std::unique_ptr<Http::TestHeaderMapImpl> upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers.get() != nullptr);
  EXPECT_EQ(upstream_headers->Host()->value(), "foo.com");
  EXPECT_EQ(upstream_headers->CacheControl()->value(), "public,123");
  ASSERT_TRUE(upstream_headers->get(Envoy::Http::LowerCaseString("foo")) != nullptr);
  EXPECT_STREQ("bar", upstream_headers->get(Envoy::Http::LowerCaseString("foo"))->value().c_str());
  ASSERT_TRUE(upstream_headers->get(Envoy::Http::LowerCaseString("eep")) != nullptr);
  EXPECT_STREQ("baz", upstream_headers->get(Envoy::Http::LowerCaseString("eep"))->value().c_str());
}

void HttpIntegrationTest::testEquivalent(const std::string& request) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    // Clone the whole listener.
    auto static_resources = bootstrap.mutable_static_resources();
    auto* old_listener = static_resources->mutable_listeners(0);
    auto* cloned_listener = static_resources->add_listeners();
    cloned_listener->CopyFrom(*old_listener);
    old_listener->set_name("http_forward");
  });
  // Set the first listener to allow absoute URLs.
  config_helper_.addConfigModifier(&setAllowAbsoluteUrl);
  initialize();

  std::string response1;
  sendRawHttpAndWaitForResponse(lookupPort("http"), request.c_str(), &response1, true);

  std::string response2;
  sendRawHttpAndWaitForResponse(lookupPort("http_forward"), request.c_str(), &response2, true);

  EXPECT_EQ(normalizeDate(response1), normalizeDate(response2));
}

void HttpIntegrationTest::testBadPath() {
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(lookupPort("http"),
                                "GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n", &response,
                                true);
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

void HttpIntegrationTest::testNoHost() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}};
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testValidZeroLengthContent() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"content-length", "0"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testInvalidContentLength() {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":authority", "host"},
                                                          {"content-length", "-1"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

void HttpIntegrationTest::testMultipleContentLengths() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":authority", "host"},
                                                          {"content-length", "3,2"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

void HttpIntegrationTest::testOverlyLongHeaders() {
  Http::TestHeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addCopy("big", std::string(60 * 1024, 'a'));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string long_value(7500, 'x');
  auto encoder_decoder = codec_client_->startRequest(big_headers);
  auto response = std::move(encoder_decoder.second);

  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("431", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testUpstreamProtocolError() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test/long/url"}, {":authority", "host"}});
  auto response = std::move(encoder_decoder.second);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  // TODO(mattklein123): Waiting for exact amount of data is a hack. This needs to
  // be fixed.
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(187, &data));
  ASSERT_TRUE(fake_upstream_connection->write("bad protocol data!"));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

void HttpIntegrationTest::testDownstreamResetBeforeResponseComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"cookie", "a=b"},
                                                          {"cookie", "c=d"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 0, true);
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Cookie)->value(), "a=b; c=d");

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(512, false);

  response->waitForBodyData(512);
  codec_client_->sendReset(*request_encoder_);

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testTrailers(uint64_t request_size, uint64_t response_size) {
  Http::TestHeaderMapImpl request_trailers{{"request1", "trailer1"}, {"request2", "trailer2"}};
  Http::TestHeaderMapImpl response_trailers{{"response1", "trailer1"}, {"response2", "trailer2"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, request_size, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(response_size, false);
  upstream_request_->encodeTrailers(response_trailers);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));
  }

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}
} // namespace Envoy
