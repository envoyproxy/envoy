#include "test/integration/http_integration.h"

#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"
#include "envoy/registry/registry.h"

#include "source/common/api/api_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread_annotations.h"
#include "source/common/http/headers.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/upstream/upstream_impl.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/quic_client_transport_socket_factory.h"
#endif

#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/common/upstream/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/test_host_predicate_config.h"
#include "test/integration/utility.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

#include "absl/time/time.h"
#include "base_integration_test.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::CodecType
typeToCodecType(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        HTTP1;
  case Http::CodecType::HTTP2:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        HTTP2;
  case Http::CodecType::HTTP3:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        HTTP3;
  default:
    RELEASE_ASSERT(0, "");
  }
}

} // namespace

IntegrationCodecClient::IntegrationCodecClient(
    Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
    Network::ClientConnectionPtr&& conn, Upstream::HostDescriptionConstSharedPtr host_description,
    Http::CodecType type)
    : IntegrationCodecClient(dispatcher, random, std::move(conn), std::move(host_description), type,
                             true) {}

IntegrationCodecClient::IntegrationCodecClient(
    Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
    Network::ClientConnectionPtr&& conn, Upstream::HostDescriptionConstSharedPtr host_description,
    Http::CodecType type, bool wait_till_connected)
    : CodecClientProd(type, std::move(conn), host_description, dispatcher, random, nullptr),
      dispatcher_(dispatcher), callbacks_(*this, wait_till_connected), codec_callbacks_(*this),
      codec_client_callbacks_(*this) {
  connection_->addConnectionCallbacks(callbacks_);
  setCodecConnectionCallbacks(codec_callbacks_);
  setCodecClientCallbacks(codec_client_callbacks_);
  if (wait_till_connected) {
    dispatcher.run(Event::Dispatcher::RunType::Block);
  }
}

void IntegrationCodecClient::flushWrite() {
  connection_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
  // NOTE: We should run blocking until all the body data is flushed.
}

IntegrationStreamDecoderPtr
IntegrationCodecClient::makeHeaderOnlyRequest(const Http::RequestHeaderMap& headers) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::RequestEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, true).IgnoreError();
  flushWrite();
  return response;
}

IntegrationStreamDecoderPtr
IntegrationCodecClient::makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                            uint64_t body_size, bool end_stream) {
  return makeRequestWithBody(headers, std::string(body_size, 'a'), end_stream);
}

IntegrationStreamDecoderPtr
IntegrationCodecClient::makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                            const std::string& body, bool end_stream) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::RequestEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, false).IgnoreError();
  Buffer::OwnedImpl data(body);
  encoder.encodeData(data, end_stream);
  flushWrite();
  return response;
}

void IntegrationCodecClient::sendData(Http::RequestEncoder& encoder, absl::string_view data,
                                      bool end_stream) {
  Buffer::OwnedImpl buffer_data(data.data(), data.size());
  encoder.encodeData(buffer_data, end_stream);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::RequestEncoder& encoder, Buffer::Instance& data,
                                      bool end_stream) {
  encoder.encodeData(data, end_stream);
  flushWrite();
}

void IntegrationCodecClient::sendData(Http::RequestEncoder& encoder, uint64_t size,
                                      bool end_stream) {
  Buffer::OwnedImpl data(std::string(size, 'a'));
  sendData(encoder, data, end_stream);
}

void IntegrationCodecClient::sendTrailers(Http::RequestEncoder& encoder,
                                          const Http::RequestTrailerMap& trailers) {
  encoder.encodeTrailers(trailers);
  flushWrite();
}

void IntegrationCodecClient::sendReset(Http::RequestEncoder& encoder) {
  encoder.getStream().resetStream(Http::StreamResetReason::LocalReset);
  flushWrite();
}

void IntegrationCodecClient::sendMetadata(Http::RequestEncoder& encoder,
                                          Http::MetadataMap metadata_map) {
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  encoder.encodeMetadata(metadata_map_vector);
  flushWrite();
}

std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr>
IntegrationCodecClient::startRequest(const Http::RequestHeaderMap& headers,
                                     bool header_only_request) {
  auto response = std::make_unique<IntegrationStreamDecoder>(dispatcher_);
  Http::RequestEncoder& encoder = newStream(*response);
  encoder.getStream().addCallbacks(*response);
  encoder.encodeHeaders(headers, /*end_stream=*/header_only_request).IgnoreError();
  flushWrite();
  return {encoder, std::move(response)};
}

AssertionResult IntegrationCodecClient::waitForDisconnect(std::chrono::milliseconds time_to_wait) {
  if (disconnected_) {
    return AssertionSuccess();
  }
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
    if (time_to_wait == TestUtility::DefaultTimeout) {
      ADD_FAILURE() << "Please don't waitForDisconnect with a 5s timeout if failure is expected\n";
    }
    return AssertionFailure() << "Timed out waiting for disconnect";
  }
  EXPECT_TRUE(disconnected_);

  return AssertionSuccess();
}

void IntegrationCodecClient::ConnectionCallbacks::onEvent(Network::ConnectionEvent event) {
  parent_.last_connection_event_ = event;
  if (event == Network::ConnectionEvent::Connected) {
    parent_.connected_ = true;
    if (block_till_connected_) {
      parent_.connection_->dispatcher().exit();
    }
  } else if (event == Network::ConnectionEvent::RemoteClose) {
    parent_.disconnected_ = true;
    parent_.connection_->dispatcher().exit();
  } else {
    if (parent_.type() == Http::CodecType::HTTP3 && !parent_.connected_ && block_till_connected_) {
      // Before handshake gets established, any connection failure should exit the loop. I.e. a QUIC
      // connection may fail of INVALID_VERSION if both this client doesn't support any of the
      // versions the server advertised before handshake established. In this case the connection is
      // closed locally and this is in a blocking event loop.
      parent_.connection_->dispatcher().exit();
    }
    parent_.disconnected_ = true;
  }
}

Network::ClientConnectionPtr HttpIntegrationTest::makeClientConnectionWithOptions(
    uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) {
  if (downstream_protocol_ <= Http::CodecType::HTTP2) {
    return BaseIntegrationTest::makeClientConnectionWithOptions(port, options);
  }
#ifdef ENVOY_ENABLE_QUIC
  // Setting socket options is not supported for HTTP3.
  Network::Address::InstanceConstSharedPtr server_addr = *Network::Utility::resolveUrl(
      fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
  Network::Address::InstanceConstSharedPtr local_addr =
      Network::Test::getCanonicalLoopbackAddress(version_);
  return Quic::createQuicNetworkConnection(
      *quic_connection_persistent_info_, quic_transport_socket_factory_->getCryptoConfig(),
      quic::QuicServerId(
          quic_transport_socket_factory_->clientContextConfig()->serverNameIndication(),
          static_cast<uint16_t>(port)),
      *dispatcher_, server_addr, local_addr, quic_stat_names_, {}, *stats_store_.rootScope(),
      options, nullptr, connection_id_generator_, *quic_transport_socket_factory_);
#else
  ASSERT(false, "running a QUIC integration test without compiling QUIC");
  return nullptr;
#endif
}

IntegrationCodecClientPtr HttpIntegrationTest::makeHttpConnection(uint32_t port) {
  return makeHttpConnection(makeClientConnection(port));
}

IntegrationCodecClientPtr HttpIntegrationTest::makeRawHttpConnection(
    Network::ClientConnectionPtr&& conn,
    absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options,
    absl::optional<envoy::config::core::v3::HttpProtocolOptions> common_http_options,
    bool wait_till_connected) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  cluster->max_response_headers_count_ = 200;
  if (!http2_options.has_value()) {
    http2_options = Http2::Utility::initializeAndValidateOptions(
                        envoy::config::core::v3::Http2ProtocolOptions())
                        .value();
    http2_options.value().set_allow_connect(true);
    http2_options.value().set_allow_metadata(true);
  }
#ifdef ENVOY_ENABLE_QUIC
  cluster->http3_options_ = ConfigHelper::http2ToHttp3ProtocolOptions(
      http2_options.value(), quic::kStreamReceiveWindowLimit);
  cluster->http3_options_.set_allow_extended_connect(true);
  cluster->http3_options_.set_allow_metadata(true);
#endif

  cluster->http2_options_ = http2_options.value();
  cluster->http1_settings_.enable_trailers_ = true;

  if (common_http_options.has_value()) {
    cluster->common_http_protocol_options_ = common_http_options.value();
  }

  if (!disable_client_header_validation_) {
    cluster->header_validator_factory_ = IntegrationUtil::makeHeaderValidationFactory(
        ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig());
  }

  Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
      cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)),
      timeSystem())};
  // This call may fail in QUICHE because of INVALID_VERSION. QUIC connection doesn't support
  // in-connection version negotiation.
  auto codec = std::make_unique<IntegrationCodecClient>(*dispatcher_, random_, std::move(conn),
                                                        host_description, downstream_protocol_,
                                                        wait_till_connected);
  if (downstream_protocol_ == Http::CodecType::HTTP3 && codec->disconnected()) {
    // Connection may get closed during version negotiation or handshake.
    // TODO(#8479) QUIC connection doesn't support in-connection version negotiationPropagate
    // INVALID_VERSION error to caller and let caller to use server advertised version list to
    // create a new connection with mutually supported version and make client codec again.
    ENVOY_LOG(error, "Fail to connect to server with error: {}",
              codec->connection()->transportFailureReason());
  }
  return codec;
}

IntegrationCodecClientPtr
HttpIntegrationTest::makeHttpConnection(Network::ClientConnectionPtr&& conn) {
  auto codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  EXPECT_TRUE(codec->connected()) << codec->connection()->transportFailureReason();
  return codec;
}

HttpIntegrationTest::HttpIntegrationTest(Http::CodecType downstream_protocol,
                                         Network::Address::IpVersion version,
                                         const std::string& config)
    : HttpIntegrationTest::HttpIntegrationTest(
          downstream_protocol,
          [version](int) {
            return Network::Utility::parseInternetAddressNoThrow(
                Network::Test::getLoopbackAddressString(version), 0);
          },
          version, config) {}

HttpIntegrationTest::HttpIntegrationTest(Http::CodecType downstream_protocol,
                                         const InstanceConstSharedPtrFn& upstream_address_fn,
                                         Network::Address::IpVersion version,
                                         const std::string& config)
    : BaseIntegrationTest(upstream_address_fn, version, config),
      downstream_protocol_(downstream_protocol), quic_stat_names_(stats_store_.symbolTable()) {
  // Legacy integration tests expect the default listener to be named "http" for
  // lookupPort calls.
  config_helper_.renameListener("http");
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
  // Allow extension lookup by name in the integration tests.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.no_extension_lookup_by_name",
                                    "false");

  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* range = hcm.mutable_internal_address_config()->add_cidr_ranges();
        // Set loopback to be trusted so tests can set x-envoy headers.
        range->set_address_prefix("127.0.0.1");
        range->mutable_prefix_len()->set_value(32);
        // Legacy tests also set XFF: 10.0.0.1
        range->set_address_prefix("10.0.0.0");
        range->mutable_prefix_len()->set_value(8);
        range = hcm.mutable_internal_address_config()->add_cidr_ranges();
        range->set_address_prefix("::1");
        range->mutable_prefix_len()->set_value(128);
      });
}

void HttpIntegrationTest::useAccessLog(
    absl::string_view format,
    std::vector<envoy::config::core::v3::TypedExtensionConfig> formatters) {
  access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  ASSERT_TRUE(config_helper_.setAccessLog(access_log_name_, format, formatters));
}

HttpIntegrationTest::~HttpIntegrationTest() {
  // Make sure any open streams have been closed. If there's an open stream, the decoder will
  // be out of scope, and so open streams result in writing to freed memory.
  if (codec_client_) {
    EXPECT_EQ(codec_client_->numActiveRequests(), 0)
        << "test requires explicit cleanupUpstreamAndDownstream";
  }
  cleanupUpstreamAndDownstream();
}

void HttpIntegrationTest::initialize() {
  if (downstream_protocol_ != Http::CodecType::HTTP3) {
    return BaseIntegrationTest::initialize();
  }
#ifdef ENVOY_ENABLE_QUIC
  // Needs to be instantiated before base class calls initialize() which starts a QUIC listener
  // according to the config.
  quic_transport_socket_factory_ = IntegrationUtil::createQuicUpstreamTransportSocketFactory(
      *api_, stats_store_, context_manager_, thread_local_, san_to_match_);

  // Needed to config QUIC transport socket factory, and needs to be added before base class calls
  // initialize().
  config_helper_.addQuicDownstreamTransportSocketConfig(enable_quic_early_data_, custom_alpns_);

  BaseIntegrationTest::initialize();
  registerTestServerPorts({"http"}, test_server_);

  // Needs to outlive all QUIC connections.
  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  auto quic_connection_persistent_info =
      Quic::createPersistentQuicInfoForCluster(*dispatcher_, *cluster);
  // Config IETF QUIC flow control window.
  quic_connection_persistent_info->quic_config_
      .SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
          Http3::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  // Config Google QUIC flow control window.
  quic_connection_persistent_info->quic_config_.SetInitialStreamFlowControlWindowToSend(
      Http3::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  // Adjust timeouts.
  quic::QuicTime::Delta connect_timeout = quic::QuicTime::Delta::FromSeconds(5 * TIMEOUT_FACTOR);
  quic_connection_persistent_info->quic_config_.set_max_time_before_crypto_handshake(
      connect_timeout);
  quic_connection_persistent_info->quic_config_.set_max_idle_time_before_crypto_handshake(
      connect_timeout);

  quic_connection_persistent_info_ = std::move(quic_connection_persistent_info);
#else
  ASSERT(false, "running a QUIC integration test without compiling QUIC");
#endif
}

void HttpIntegrationTest::setupHttp1ImplOverrides(Http1ParserImpl http1_implementation) {
  switch (http1_implementation) {
  case Http1ParserImpl::HttpParser:
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_use_balsa_parser", "false");
    break;
  case Http1ParserImpl::BalsaParser:
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_use_balsa_parser", "true");
    break;
  }
}

void HttpIntegrationTest::setupHttp2ImplOverrides(Http2Impl http2_implementation) {
  switch (http2_implementation) {
  case Http2Impl::Nghttp2:
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http2_use_oghttp2", "false");
    break;
  case Http2Impl::Oghttp2:
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http2_use_oghttp2", "true");
    break;
  }
}

void HttpIntegrationTest::setDownstreamProtocol(Http::CodecType downstream_protocol) {
  downstream_protocol_ = downstream_protocol;
  config_helper_.setClientCodec(typeToCodecType(downstream_protocol_));
}

ConfigHelper::HttpModifierFunction HttpIntegrationTest::setEnableDownstreamTrailersHttp1() {
  return [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.mutable_http_protocol_options()->set_enable_trailers(true); };
}

ConfigHelper::ConfigModifierFunction HttpIntegrationTest::setEnableUpstreamTrailersHttp1() {
  return [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()
          ->mutable_http_protocol_options()
          ->set_enable_trailers(true);
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    }
  };
}

ConfigHelper::HttpModifierFunction HttpIntegrationTest::configureProxyStatus() {
  return [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
    auto* psc = hcm.mutable_proxy_status_config();
    psc->set_set_recommended_response_code(false);
  };
}

IntegrationStreamDecoderPtr HttpIntegrationTest::sendRequestAndWaitForResponse(
    const Http::TestRequestHeaderMapImpl& request_headers, uint32_t request_body_size,
    const Http::TestResponseHeaderMapImpl& response_headers, uint32_t response_body_size,
    const std::vector<uint64_t>& upstream_indices, std::chrono::milliseconds timeout) {
  ASSERT(codec_client_ != nullptr);
  // Send the request to Envoy.
  IntegrationStreamDecoderPtr response;
  if (request_body_size) {
    response = codec_client_->makeRequestWithBody(request_headers, request_body_size);
  } else {
    response = codec_client_->makeHeaderOnlyRequest(request_headers);
  }
  waitForNextUpstreamRequest(upstream_indices, timeout);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(response_headers, response_body_size == 0);
  // Send any response data, with end_stream true.
  if (response_body_size) {
    upstream_request_->encodeData(response_body_size, true);
  }
  // Wait for the response to be read by the codec client.
  RELEASE_ASSERT(response->waitForEndStream(timeout),
                 fmt::format("unexpected timeout after ", timeout.count(), " ms"));
  return response;
}

IntegrationStreamDecoderPtr HttpIntegrationTest::sendRequestAndWaitForResponse(
    const Http::TestRequestHeaderMapImpl& request_headers, uint32_t request_body_size,
    const Http::TestResponseHeaderMapImpl& response_headers, uint32_t response_body_size,
    uint64_t upstream_index, std::chrono::milliseconds timeout) {
  return sendRequestAndWaitForResponse(request_headers, request_body_size, response_headers,
                                       response_body_size, std::vector<uint64_t>{upstream_index},
                                       timeout);
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
    fake_upstream_connection_.reset();
  }
  if (codec_client_) {
    codec_client_->close();
  }
}

void HttpIntegrationTest::sendRequestAndVerifyResponse(
    const Http::TestRequestHeaderMapImpl& request_headers, const int request_size,
    const Http::TestResponseHeaderMapImpl& response_headers, const int response_size,
    const int backend_idx,
    absl::optional<const Http::TestResponseHeaderMapImpl> expected_response_headers) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                response_size, backend_idx);
  verifyResponse(std::move(response), "200",
                 (expected_response_headers.has_value()) ? *expected_response_headers
                                                         : response_headers,
                 std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  cleanupUpstreamAndDownstream();
}

void HttpIntegrationTest::verifyResponse(IntegrationStreamDecoderPtr response,
                                         const std::string& response_code,
                                         const Http::TestResponseHeaderMapImpl& expected_headers,
                                         const std::string& expected_body) {
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response_code, response->headers().getStatusValue());
  expected_headers.iterate([response_headers = &response->headers()](
                               const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    const auto entry =
        response_headers->get(Http::LowerCaseString{std::string(header.key().getStringView())});
    EXPECT_FALSE(entry.empty());
    EXPECT_EQ(header.value().getStringView(), entry[0]->value().getStringView());
    return Http::HeaderMap::Iterate::Continue;
  });

  EXPECT_EQ(response->body(), expected_body);
}

absl::optional<uint64_t> HttpIntegrationTest::waitForNextUpstreamConnection(
    const std::vector<uint64_t>& upstream_indices,
    std::chrono::milliseconds connection_wait_timeout,
    FakeHttpConnectionPtr& fake_upstream_connection) {
  AssertionResult result = AssertionFailure();
  int upstream_index = 0;
  Event::TestTimeSystem::RealTimeBound bound(connection_wait_timeout);
  // Loop over the upstreams until the call times out or an upstream request is received.
  while (!result) {
    upstream_index = upstream_index % upstream_indices.size();
    result = fake_upstreams_[upstream_indices[upstream_index]]->waitForHttpConnection(
        *dispatcher_, fake_upstream_connection, std::chrono::milliseconds(5));
    if (result) {
      return upstream_index;
    } else if (!bound.withinBound()) {
      RELEASE_ASSERT(0, "Timed out waiting for new connection.");
      break;
    }
    ++upstream_index;
  }
  RELEASE_ASSERT(result, result.message());
  return {};
}

absl::optional<uint64_t>
HttpIntegrationTest::waitForNextUpstreamRequest(const std::vector<uint64_t>& upstream_indices,
                                                std::chrono::milliseconds connection_wait_timeout) {
  absl::optional<uint64_t> upstream_with_request;
  // If there is no upstream connection, wait for it to be established.
  if (!fake_upstream_connection_) {
    upstream_with_request = waitForNextUpstreamConnection(upstream_indices, connection_wait_timeout,
                                                          fake_upstream_connection_);
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

void HttpIntegrationTest::waitForNextUpstreamRequest(
    uint64_t upstream_index, std::chrono::milliseconds connection_wait_timeout) {
  waitForNextUpstreamRequest(std::vector<uint64_t>({upstream_index}), connection_wait_timeout);
}

void HttpIntegrationTest::checkSimpleRequestSuccess(uint64_t expected_request_size,
                                                    uint64_t expected_response_size,
                                                    IntegrationStreamDecoder* response) {
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(expected_request_size, upstream_request_->bodyLength());

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(expected_response_size, response->body().size());
}

void HttpIntegrationTest::testRouterRequestAndResponseWithBody(
    uint64_t request_size, uint64_t response_size, bool big_header, bool set_content_length_header,
    ConnectionCreationFunction* create_connection, std::chrono::milliseconds timeout) {
#ifdef ENVOY_CONFIG_COVERAGE
  // Avoid excessive logging at UDP packet level, which causes log spamming, as well as worse
  // contention: https://github.com/envoyproxy/envoy/issues/19595
  ENVOY_LOG_MISC(warn, "manually lowering logs to error");
  LogLevelSetter save_levels(spdlog::level::err);
#endif
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  if (set_content_length_header) {
    default_request_headers_.setContentLength(request_size);
    default_response_headers_.setContentLength(response_size);
  }
  if (big_header) {
    default_request_headers_.addCopy("big", std::string(4096, 'a'));
  }
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, request_size, default_response_headers_, response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response.get());
}

void HttpIntegrationTest::testGiantRequestAndResponse(uint64_t request_size, uint64_t response_size,
                                                      bool set_content_length_header,
                                                      std::chrono::milliseconds timeout) {
  autonomous_upstream_ = true;
#ifdef ENVOY_CONFIG_COVERAGE
  // Avoid excessive logging at UDP packet level, which causes log spamming, as well as worse
  // contention: https://github.com/envoyproxy/envoy/issues/19595
  ENVOY_LOG_MISC(warn, "manually lowering logs to error");
  LogLevelSetter save_levels(spdlog::level::err);
#endif
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(response_size)},
      {AutonomousStream::EXPECT_REQUEST_SIZE_BYTES, std::to_string(request_size)},
      {AutonomousStream::NO_TRAILERS, "0"}};
  auto response_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(default_response_headers_);
  if (set_content_length_header) {
    request_headers.setContentLength(request_size);
    response_headers->setContentLength(response_size);
  }
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::move(response_headers));

  auto response = codec_client_->makeRequestWithBody(request_headers, request_size);

  // Wait for the response to be read by the codec client.
  RELEASE_ASSERT(response->waitForEndStream(timeout), "unexpected timeout");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response_size, response->body().size());
}

void HttpIntegrationTest::testRouterUpstreamProtocolError(const std::string& expected_code,
                                                          const std::string& expected_flag) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_FLAGS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test/long/url"}, {":authority", "sni.lyft.com"}});
  auto response = std::move(encoder_decoder.second);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  ASSERT_TRUE(fake_upstream_connection->write("bad protocol data!"));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ(expected_code, response->headers().getStatusValue());
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr(expected_code));
  EXPECT_THAT(log, HasSubstr(expected_flag));
}

IntegrationStreamDecoderPtr
HttpIntegrationTest::makeHeaderOnlyRequest(ConnectionCreationFunction* create_connection,
                                           int upstream_index, const std::string& path,
                                           const std::string& authority) {
  // This is called multiple times per test in ads_integration_test. Only call
  // initialize() the first time.
  if (!initialized()) {
    initialize();
  }
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  if (!authority.empty()) {
    default_request_headers_.setHost(authority);
  }
  default_request_headers_.setPath(path);
  return sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0,
                                       upstream_index);
}

void HttpIntegrationTest::testRouterHeaderOnlyRequestAndResponse(
    ConnectionCreationFunction* create_connection, int upstream_index, const std::string& path,
    const std::string& authority) {
  auto response = makeHeaderOnlyRequest(create_connection, upstream_index, path, authority);
  checkSimpleRequestSuccess(0U, 0U, response.get());
}

// Change the default route to be restrictive, and send a request to an alternate route.
void HttpIntegrationTest::testRouterNotFound() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=destination_not_found; details=\"route_not_found; NR\"");
}

// Change the default route to be restrictive, and send a POST to an alternate route.
void HttpIntegrationTest::testRouterNotFoundWithBody() {
  config_helper_.setDefaultHostAndRoute("foo.com", "/found");
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/notfound", "foo", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=destination_not_found; details=\"route_not_found; NR\"");
}

// Make sure virtual cluster stats are charged to the appropriate virtual cluster.
void HttpIntegrationTest::testRouterVirtualClusters() {
  const std::string matching_header = "x-use-test-vcluster";
  config_helper_.addConfigModifier(
      [matching_header](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* route_config = hcm.mutable_route_config();
        ASSERT_EQ(1, route_config->virtual_hosts_size());
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        {
          auto* virtual_cluster = virtual_host->add_virtual_clusters();
          virtual_cluster->set_name("test_vcluster");
          auto* headers = virtual_cluster->add_headers();
          headers->set_name(matching_header);
          headers->set_present_match(true);
        }
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {matching_header, "true"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  checkSimpleRequestSuccess(0, 0, response.get());

  test_server_->waitForCounterEq("vhost.integration.vcluster.test_vcluster.upstream_rq_total", 1);
  test_server_->waitForCounterEq("vhost.integration.vcluster.other.upstream_rq_total", 0);

  auto response2 =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  checkSimpleRequestSuccess(0, 0, response2.get());

  test_server_->waitForCounterEq("vhost.integration.vcluster.test_vcluster.upstream_rq_total", 1);
  test_server_->waitForCounterEq("vhost.integration.vcluster.other.upstream_rq_total", 1);
}

// Make sure route level stats are generated correctly.
void HttpIntegrationTest::testRouteStats() {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route_config = hcm.mutable_route_config();
        ASSERT_EQ(1, route_config->virtual_hosts_size());
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0);
        route->set_stat_prefix("test_route");
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  checkSimpleRequestSuccess(0, 0, response.get());

  test_server_->waitForCounterEq("vhost.integration.route.test_route.upstream_rq_total", 1);
  test_server_->waitForCounterEq("vhost.integration.route.test_route.upstream_rq_completed", 1);
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeRequestComplete() {
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_EQ(response->headers().getProxyStatusValue(),
              "envoy; error=connection_terminated; "
              "details=\"upstream_reset_before_response_started{connection_termination|QUIC_NO_"
              "ERROR|FROM_PEER|Closed_by_application}; UC\"");
  } else {
    EXPECT_EQ(response->headers().getProxyStatusValue(),
              "envoy; error=connection_terminated; "
              "details=\"upstream_reset_before_response_started{connection_termination}; UC\"");
  }
}

void HttpIntegrationTest::testRouterUpstreamDisconnectBeforeResponseComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

void HttpIntegrationTest::testRouterDownstreamDisconnectBeforeRequestComplete(
    ConnectionCreationFunction* create_connection) {
  initialize();

  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->close();

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
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
#if defined(__APPLE__) || defined(WIN32)
  // Skip this test on OS/X + Windows: we can't detect the early close, and we
  // won't clean up the upstream connection until it times out. See #4294.
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }
#endif
  initialize();
  codec_client_ = makeHttpConnection(
      create_connection ? ((*create_connection)()) : makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, false);
  response->waitForBodyData(512);
  codec_client_->close();

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testRouterUpstreamResponseBeforeRequestComplete(uint32_t status_code) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  if (status_code != 0) {
    default_response_headers_.setStatus(status_code);
  }
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ(status_code != 0 ? absl::StrCat(status_code) : "200",
            response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testRouterRetryOnResetBeforeRequestAfterHeaders() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers = {{":method", "POST"},
                                            {":path", "/test/long/url"},
                                            {":scheme", "http"},
                                            {":authority", "sni.lyft.com"},
                                            {"x-forwarded-for", "10.0.0.1"},
                                            {"x-envoy-retry-on", "reset-before-request"}};
  auto encoder_decoder = codec_client_->startRequest(headers);
  waitForNextUpstreamConnection(std::vector<uint64_t>({0}), TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  auto status = request_encoder_->encodeHeaders(headers, false);
  // Make sure we transmit headers successfully
  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  // Reset the upstream connection after the headers have been sent
  ASSERT_TRUE(fake_upstream_connection_->close());

  // We should get no new stream/retry
  ASSERT_FALSE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  cleanupUpstreamAndDownstream();
}

void HttpIntegrationTest::testRouterRetryOnResetBeforeRequestBeforeHeaders() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "reset-before-request"}});
  waitForNextUpstreamConnection(std::vector<uint64_t>({0}), TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  // Reset the upstream connection before the headers have been sent
  ASSERT_TRUE(fake_upstream_connection_->close());
  // We should get a retry
  waitForNextUpstreamConnection(std::vector<uint64_t>({0}), TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  // Send the request body to unblock the buffer filter
  codec_client_->sendData(*request_encoder_, 1024, true);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);

  ASSERT_TRUE(response->waitForEndStream());

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  codec_client_->close();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1024U, response->body().size());
}

void HttpIntegrationTest::testRetry() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

// Tests that the x-envoy-attempt-count header is properly set on the upstream request
// and updated after the request is retried.
void HttpIntegrationTest::testRetryAttemptCountHeader() {
  auto host = config_helper_.createVirtualHost("sni.lyft.com", "/test_retry");
  host.set_include_request_attempt_count(true);
  host.set_include_attempt_count_in_response(true);
  config_helper_.addVirtualHost(host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test_retry"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  EXPECT_EQ(atoi(std::string(upstream_request_->headers().getEnvoyAttemptCountValue()).c_str()), 1);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  EXPECT_EQ(atoi(std::string(upstream_request_->headers().getEnvoyAttemptCountValue()).c_str()), 2);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
  EXPECT_EQ(2, atoi(std::string(response->headers().getEnvoyAttemptCountValue()).c_str()));
}

void HttpIntegrationTest::testGrpcRetry() {
  Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                     {"grpc-status", "0"}};
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-grpc-on", "cancelled"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1024, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"grpc-status", "1"}}, false);
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, fake_upstreams_[0]->httpType() != Http::CodecType::HTTP2);
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP2) {
    upstream_request_->encodeTrailers(response_trailers);
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
  }
}

void HttpIntegrationTest::testEnvoyHandling1xx(bool additional_continue_from_upstream,
                                               const std::string& via, bool disconnect_after_100) {
  useAccessLog("%RESPONSE_CODE%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"expect", "100-contINUE"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  // The continue headers should arrive immediately.
  response->waitFor1xxHeaders();
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send the rest of the request.
  codec_client_->sendData(*request_encoder_, 10, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // Verify the Expect header is stripped.
  EXPECT_TRUE(upstream_request_->headers().get(Http::Headers::get().Expect).empty());
  if (via.empty()) {
    EXPECT_TRUE(upstream_request_->headers().get(Http::Headers::get().Via).empty());
  } else {
    EXPECT_EQ(
        via,
        upstream_request_->headers().get(Http::Headers::get().Via)[0]->value().getStringView());
  }

  if (additional_continue_from_upstream) {
    // Make sure if upstream sends an 100-Continue Envoy doesn't send its own and proxy the one
    // from upstream!
    upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  }

  if (disconnect_after_100) {
    response->waitFor1xxHeaders();
    codec_client_->close();
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("100"));
    ASSERT_TRUE(fake_upstream_connection_->close());
    return;
  }

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(12, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  ASSERT(response->informationalHeaders() != nullptr);
  EXPECT_EQ("100", response->informationalHeaders()->getStatusValue());
  EXPECT_EQ(nullptr, response->informationalHeaders()->Via());
  EXPECT_EQ("200", response->headers().getStatusValue());
  if (via.empty()) {
    EXPECT_EQ(nullptr, response->headers().Via());
  } else {
    EXPECT_EQ(via.c_str(), response->headers().getViaValue());
  }
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("200"));
}

void HttpIntegrationTest::testEnvoyProxying1xx(bool continue_before_upstream_complete,
                                               bool with_encoder_filter,
                                               bool with_multiple_1xx_headers,
                                               absl::string_view initial_code) {
  if (with_encoder_filter) {
    // Add a filter to make sure 100s play well with them.
    config_helper_.prependFilter("name: passthrough-filter");
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"expect", "100-contINUE"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // This case tests sending on 100-Continue headers before the client has sent all the
  // request data.
  if (continue_before_upstream_complete) {
    upstream_request_->encode1xxHeaders(
        Http::TestResponseHeaderMapImpl{{":status", initial_code.data()}});
    if (with_multiple_1xx_headers) {
      upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "102"}}, false);
      upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
    }
    response->waitFor1xxHeaders();
  }
  // Send all of the request data and wait for it to be received upstream.
  codec_client_->sendData(*request_encoder_, 10, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // This case tests forwarding 100-Continue after the client has sent all data.
  if (!continue_before_upstream_complete) {
    upstream_request_->encode1xxHeaders(
        Http::TestResponseHeaderMapImpl{{":status", initial_code.data()}});
    if (with_multiple_1xx_headers) {
      upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "102"}}, false);
      upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
    }
    response->waitFor1xxHeaders();
  }
  // Now send the rest of the response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  ASSERT(response->informationalHeaders() != nullptr);
  EXPECT_EQ(initial_code, response->informationalHeaders()->getStatusValue());

  EXPECT_EQ("200", response->headers().getStatusValue());
}

void HttpIntegrationTest::testTwoRequests(bool network_backup) {
  // if network_backup is false, this simply tests that Envoy can handle multiple
  // requests on a connection.
  //
  // If network_backup is true, the first request will explicitly set the TCP level flow control
  // as blocked as it finishes the encode and set a timer to unblock. The second stream should be
  // created while the socket appears to be in the high watermark state, and regression tests that
  // flow control will be corrected as the socket "becomes unblocked"
  if (network_backup) {
    config_helper_.prependFilter(
        fmt::format(R"EOF(
  name: pause-filter{}
  )EOF",
                    downstreamProtocol() == Http::CodecType::HTTP3 ? "-for-quic" : ""));
  }
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Request 1.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());

  // Request 2.
  response = codec_client_->makeRequestWithBody(default_request_headers_, 512);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1024, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(512U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1024U, response->body().size());
}

void HttpIntegrationTest::testLargeRequestUrl(uint32_t url_size, uint32_t max_headers_size) {
  // `size` parameter dictates the size of each header that will be added to the request and `count`
  // parameter is the number of headers to be added. The actual request byte size will exceed `size`
  // due to the keys and other headers. The actual request header count will exceed `count` by four
  // due to default headers.

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_max_request_headers_kb()->set_value(max_headers_size); });
  setMaxRequestHeadersKb(max_headers_size);

  Http::TestRequestHeaderMapImpl big_headers{{":method", "GET"},
                                             {":path", "/" + std::string(url_size * 1024, 'a')},
                                             {":scheme", "http"},
                                             {":authority", "sni.lyft.com"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  if (url_size >= max_headers_size) {
    // header size includes keys too, so expect rejection when equal
    auto encoder_decoder = codec_client_->startRequest(big_headers);
    auto response = std::move(encoder_decoder.second);

    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("431", response->headers().Status()->value().getStringView());
    } else {
      ASSERT_TRUE(response->waitForReset());
      codec_client_->close();
    }
  } else {
    auto response = sendRequestAndWaitForResponse(big_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }
}

void HttpIntegrationTest::testLargeRequestHeaders(uint32_t size, uint32_t count, uint32_t max_size,
                                                  uint32_t max_count,
                                                  std::chrono::milliseconds timeout) {
  autonomous_upstream_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  // `size` parameter dictates the size of each header that will be added to the request and `count`
  // parameter is the number of headers to be added. The actual request byte size will exceed `size`
  // due to the keys and other headers. The actual request header count will exceed `count` by four
  // due to default headers.

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(max_size);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(
            max_count);
      });
  setMaxRequestHeadersKb(max_size);
  setMaxRequestHeadersCount(max_count);

  Http::TestRequestHeaderMapImpl big_headers{{":method", "GET"},
                                             {":path", "/test/long/url"},
                                             {":scheme", "http"},
                                             {":authority", "sni.lyft.com"}};

  // Already added four headers.
  for (unsigned int i = 0; i < count; i++) {
    big_headers.addCopy(std::to_string(i), std::string(size * 1024, 'a'));
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(
          std::make_unique<Http::TestResponseHeaderMapImpl>(default_response_headers_));

  if (size >= max_size || count > max_count) {
    // header size includes keys too, so expect rejection when equal
    auto encoder_decoder = codec_client_->startRequest(big_headers);
    auto response = std::move(encoder_decoder.second);

    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("431", response->headers().getStatusValue());
    } else {
      ASSERT_TRUE(response->waitForReset());
      codec_client_->close();
    }
  } else {
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(big_headers);
    RELEASE_ASSERT(response->waitForEndStream(timeout),
                   fmt::format("unexpected timeout after {}ms", timeout.count()));
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  if (count > max_count) {
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("too_many_headers"));
  }
}

void HttpIntegrationTest::testLargeResponseHeaders(uint32_t size, uint32_t count, uint32_t max_size,
                                                   uint32_t max_count,
                                                   std::chrono::milliseconds timeout) {
  autonomous_upstream_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  // `size` parameter dictates the size of each header that will be added to the response and
  // `count` parameter is the number of headers to be added. The actual request byte size will
  // exceed `size` due to the keys and other headers. The actual request header count will exceed
  // `count` by four due to default headers.

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_response_headers_kb()->set_value(max_size);
    http_protocol_options->mutable_max_headers_count()->set_value(max_count);

    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  // This test is validating upstream response headers, but the test client will fail to receive the
  // request from Envoy if its limits aren't increased.
  envoy::config::core::v3::HttpProtocolOptions client_protocol_options;
  client_protocol_options.mutable_max_response_headers_kb()->set_value(max_size);
  client_protocol_options.mutable_max_headers_count()->set_value(max_count);

  Http::TestRequestHeaderMapImpl big_headers(default_response_headers_);

  // Already added four headers.
  for (unsigned int i = 0; i < count; i++) {
    big_headers.addCopy(std::to_string(i), std::string(size * 1024, 'a'));
  }

  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt,
                                        client_protocol_options);
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(big_headers));

  if (size >= max_size || count > max_count) {
    // header size includes keys too, so expect rejection when equal
    auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
    auto response = std::move(encoder_decoder.second);

    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("431", response->headers().getStatusValue());
    } else {
      ASSERT_TRUE(response->waitForReset());
      codec_client_->close();
    }
  } else {
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    RELEASE_ASSERT(response->waitForEndStream(timeout),
                   fmt::format("unexpected timeout after {}ms", timeout.count()));
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
  if (count > max_count) {
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("too_many_headers"));
  }
}

void HttpIntegrationTest::testLargeRequestTrailers(uint32_t size, uint32_t max_size) {
  // `size` parameter is the size of the trailer that will be added to the
  // request. The actual request byte size will exceed `size` due to keys
  // and other headers.

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_max_request_headers_kb()->set_value(max_size); });
  setMaxRequestHeadersKb(max_size);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  request_trailers.addCopy("big", std::string(size * 1024, 'a'));

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 10, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  if (size >= max_size) {
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
      EXPECT_TRUE(response->complete());
      EXPECT_EQ("431", response->headers().getStatusValue());
    } else {
      // Expect a stream reset when the size of the trailers is larger than the maximum
      // limit.
      ASSERT_TRUE(response->waitForReset());
      codec_client_->close();
      EXPECT_FALSE(response->complete());
    }
  } else {
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
  }
}

void HttpIntegrationTest::testManyRequestHeaders(std::chrono::milliseconds time) {
  // This test uses an Http::HeaderMapImpl instead of an Http::TestHeaderMapImpl to avoid
  // time-consuming asserts when using a large number of headers.
  setMaxRequestHeadersKb(96);
  setMaxRequestHeadersCount(10010);

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(upstreamConfig().max_request_headers_kb_);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(
            upstreamConfig().max_request_headers_count_);
      });

  auto big_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().Method, "GET"},
       {Http::Headers::get().Path, "/test/long/url"},
       {Http::Headers::get().Scheme, "http"},
       {Http::Headers::get().Host, "sni.lyft.com"}});

  for (int i = 0; i < 10000; i++) {
    big_headers->addCopy(Http::LowerCaseString(std::to_string(i)), std::string(0, 'a'));
  }
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      sendRequestAndWaitForResponse(*big_headers, 0, default_response_headers_, 0, 0, time);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

void HttpIntegrationTest::testDownstreamResetBeforeResponseComplete() {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"cookie", "a=b"},
                                                                 {"cookie", "c=d"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 0, true);
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Cookie)[0]->value(), "a=b; c=d");

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, false);

  response->waitForBodyData(512);
  codec_client_->sendReset(*request_encoder_);

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
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
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

void HttpIntegrationTest::testTrailers(uint64_t request_size, uint64_t response_size,
                                       bool check_request, bool check_response) {
  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                     {"response2", "trailer2"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, request_size, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(response_size, false);
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  if (check_request) {
    EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));
  } else {
    EXPECT_EQ(upstream_request_->trailers(), nullptr);
  }

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response_size, response->body().size());
  if (check_response) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));
  } else {
    EXPECT_EQ(response->trailers(), nullptr);
  }
}

void HttpIntegrationTest::testAdminDrain(Http::CodecType admin_request_type) {
  initialize();

  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);
  Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(0);

  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Invoke drain listeners endpoint and validate that we can still work on inflight requests.
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", admin_request_type, version_);
  EXPECT_TRUE(admin_response->complete());
  EXPECT_EQ("200", admin_response->headers().getStatusValue());
  EXPECT_EQ("OK\n", admin_response->body());

  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Wait for the response to be read by the codec client.
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Validate that the listeners have been stopped.
  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);

  // Validate that port is closed and can be bound by other sockets.
  // This does not work for HTTP/3 because the port is not closed until the listener is completely
  // destroyed. TODO(danzh) Match TCP behavior as much as possible.
  if (downstreamProtocol() != Http::CodecType::HTTP3) {
    ASSERT_TRUE(waitForPortAvailable(http_port));
  }
}

void HttpIntegrationTest::simultaneousRequest(uint32_t request1_bytes, uint32_t request2_bytes,
                                              uint32_t response1_bytes, uint32_t response2_bytes) {
  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start request 1
  auto encoder_decoder1 = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder* encoder1 = &encoder_decoder1.first;
  auto response1 = std::move(encoder_decoder1.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder* encoder2 = &encoder_decoder2.first;
  auto response2 = std::move(encoder_decoder2.second);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));

  // Finish request 1
  codec_client_->sendData(*encoder1, request1_bytes, true);
  ASSERT_TRUE(upstream_request1->waitForEndStream(*dispatcher_));

  // Finish request 2
  codec_client_->sendData(*encoder2, request2_bytes, true);
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Respond to request 2
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(response2_bytes, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  EXPECT_EQ(response2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(response1_bytes, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(response1_bytes, response1->body().size());

  ASSERT_FALSE(response1->headers().get(Http::LowerCaseString("num_streams")).empty());
  ASSERT_FALSE(response2->headers().get(Http::LowerCaseString("num_streams")).empty());
  EXPECT_EQ(
      response1->headers().get(Http::LowerCaseString("num_streams"))[0]->value().getStringView(),
      "1");
  EXPECT_EQ(
      response2->headers().get(Http::LowerCaseString("num_streams"))[0]->value().getStringView(),
      upstreamProtocol() == Http::CodecType::HTTP1 ? "1" : "2");
}

std::string HttpIntegrationTest::downstreamProtocolStatsRoot() const {
  switch (downstreamProtocol()) {
  case Http::CodecClient::Type::HTTP1:
    return "http1";
  case Http::CodecClient::Type::HTTP2:
    return "http2";
  case Http::CodecClient::Type::HTTP3:
    return "http3";
  }
  return "invalid";
}

std::string HttpIntegrationTest::upstreamProtocolStatsRoot() const {
  switch (upstreamProtocol()) {
  case FakeHttpConnection::Type::HTTP1:
    return "http1";
  case FakeHttpConnection::Type::HTTP2:
    return "http2";
  case FakeHttpConnection::Type::HTTP3:
    return "http3";
  }
  return "invalid";
}

std::string HttpIntegrationTest::listenerStatPrefix(const std::string& stat_name) {
  if (version_ == Network::Address::IpVersion::v4) {
    return "listener.127.0.0.1_0." + stat_name;
  }
  return "listener.[__1]_0." + stat_name;
}

void HttpIntegrationTest::expectUpstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                                             BytesCountExpectation h2_expectation,
                                                             BytesCountExpectation h3_expectation,
                                                             const int id) {
  std::string access_log = waitForAccessLog(access_log_name_, id, true);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  switch (upstreamProtocol()) {
  case Http::CodecType::HTTP1: {
    EXPECT_EQ(h1_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h1_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h1_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h1_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }
  case Http::CodecType::HTTP2: {
    EXPECT_EQ(h2_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h2_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h2_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h2_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }
  case Http::CodecType::HTTP3: {
    EXPECT_EQ(h3_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h3_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h3_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h3_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }

  default:
    EXPECT_TRUE(false) << "Unexpected codec type: " << static_cast<int>(upstreamProtocol());
  }
}

void HttpIntegrationTest::expectDownstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                                               BytesCountExpectation h2_expectation,
                                                               BytesCountExpectation h3_expectation,
                                                               const int id) {
  std::string access_log = waitForAccessLog(access_log_name_, id);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  switch (downstreamProtocol()) {
  case Http::CodecType::HTTP1: {
    EXPECT_EQ(h1_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h1_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h1_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h1_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }
  case Http::CodecType::HTTP2: {
    EXPECT_EQ(h2_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h2_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h2_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h2_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }
  case Http::CodecType::HTTP3: {
    EXPECT_EQ(h3_expectation.wire_bytes_sent_ == 0, wire_bytes_sent == 0);
    EXPECT_EQ(h3_expectation.wire_bytes_received_ == 0, wire_bytes_received == 0);
    EXPECT_EQ(h3_expectation.header_bytes_sent_ == 0, header_bytes_sent == 0);
    EXPECT_EQ(h3_expectation.header_bytes_received_ == 0, header_bytes_received == 0);
    return;
  }
  default:
    EXPECT_TRUE(false) << "Unexpected codec type: " << static_cast<int>(downstreamProtocol());
  }
}

void Http2RawFrameIntegrationTest::startHttp2Session() {
  ASSERT_TRUE(tcp_client_->write(Http2Frame::Preamble, false, false));

  // Send empty initial SETTINGS frame.
  auto settings = Http2Frame::makeEmptySettingsFrame();
  ASSERT_TRUE(tcp_client_->write(std::string(settings), false, false));

  // Read initial SETTINGS frame from the server.
  readFrame();

  // Send an SETTINGS ACK.
  settings = Http2Frame::makeEmptySettingsFrame(Http2Frame::SettingsFlags::Ack);
  ASSERT_TRUE(tcp_client_->write(std::string(settings), false, false));

  // read pending SETTINGS and WINDOW_UPDATE frames
  readFrame();
  readFrame();
}

void Http2RawFrameIntegrationTest::beginSession() {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);
  // set lower outbound frame limits to make tests run faster
  config_helper_.setDownstreamOutboundFramesLimits(1000, 100);
  initialize();
  // Set up a raw connection to easily send requests without reading responses.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 1024));
  tcp_client_ = makeTcpConnection(lookupPort("http"), options);
  startHttp2Session();
}

Http2Frame Http2RawFrameIntegrationTest::readFrame() {
  Http2Frame frame;
  EXPECT_TRUE(tcp_client_->waitForData(frame.HeaderSize));
  frame.setHeader(tcp_client_->data());
  tcp_client_->clearData(frame.HeaderSize);
  auto len = frame.payloadSize();
  if (len) {
    EXPECT_TRUE(tcp_client_->waitForData(len));
    frame.setPayload(tcp_client_->data());
    tcp_client_->clearData(len);
  }
  return frame;
}

void Http2RawFrameIntegrationTest::sendFrame(const Http2Frame& frame) {
  ASSERT_TRUE(tcp_client_->connected());
  ASSERT_TRUE(tcp_client_->write(std::string(frame), false, false));
}

absl::string_view upstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpUpstream";
  case Http::CodecType::HTTP2:
    return "Http2Upstream";
  case Http::CodecType::HTTP3:
    return "Http3Upstream";
  }
  return "UnknownUpstream";
}

absl::string_view downstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpDownstream_";
  case Http::CodecType::HTTP2:
    return "Http2Downstream_";
  case Http::CodecType::HTTP3:
    return "Http3Downstream_";
  }
  return "UnknownDownstream";
}

absl::string_view http2ImplementationToString(Http2Impl impl) {
  switch (impl) {
  case Http2Impl::Nghttp2:
    return "Nghttp2";
  case Http2Impl::Oghttp2:
    return "Oghttp2";
  }
  return "UnknownHttp2Impl";
}

} // namespace Envoy
