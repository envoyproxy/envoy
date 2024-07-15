#include <memory>

#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/tcp_proxy/upstream.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/router/upstream_request.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::EndsWith;
using testing::Return;

namespace Envoy {
namespace TcpProxy {
namespace {
using envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy;

class HttpUpstreamTest : public testing::TestWithParam<Http::CodecType> {
public:
  HttpUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, encodeHeaders(_, false));
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(encoder_, enableTcpTunneling()).Times(AnyNumber());
    if (GetParam() == Http::CodecType::HTTP1) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
    }
    EXPECT_CALL(stream_encoder_options_, enableHalfClose()).Times(AnyNumber());
    tcp_proxy_.mutable_tunneling_config()->set_hostname("default.host.com:443");
  }

  void setupUpstream() {
    config_ = std::make_unique<TunnelingConfigHelperImpl>(scope_, tcp_proxy_, context_);
    upstream_ = std::make_unique<HttpUpstream>(callbacks_, *this->config_, downstream_stream_info_,
                                               GetParam());
    upstream_->setRequestEncoder(encoder_, true);
  }

  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  TcpProxy tcp_proxy_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  std::unique_ptr<TunnelingConfigHelper> config_;
  std::unique_ptr<HttpUpstream> upstream_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

INSTANTIATE_TEST_SUITE_P(H1H2H3Codecs, HttpUpstreamTest,
                         ::testing::Values(Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                                           Http::CodecType::HTTP3));

TEST_P(HttpUpstreamTest, WriteUpstream) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->encodeData(buffer1, false);

  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->encodeData(buffer2, true);

  // New upstream with no encoder.
  this->upstream_ = std::make_unique<HttpUpstream>(this->callbacks_, *this->config_,
                                                   this->downstream_stream_info_, GetParam());
  this->upstream_->encodeData(buffer2, true);
}

TEST_P(HttpUpstreamTest, WriteDownstream) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->responseDecoder().decodeData(buffer1, false);

  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->responseDecoder().decodeData(buffer2, true);
}

TEST_P(HttpUpstreamTest, InvalidUpgradeWithEarlyFin) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TEST_P(HttpUpstreamTest, InvalidUpgradeWithNon200) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "301"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_P(HttpUpstreamTest, ReadDisable) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, readDisable(true));
  EXPECT_TRUE(this->upstream_->readDisable(true));

  EXPECT_CALL(this->encoder_.stream_, readDisable(false));
  EXPECT_TRUE(this->upstream_->readDisable(false));

  // New upstream with no encoder.
  this->upstream_ = std::make_unique<HttpUpstream>(this->callbacks_, *this->config_,
                                                   this->downstream_stream_info_, GetParam());
  EXPECT_FALSE(this->upstream_->readDisable(true));
}

TEST_P(HttpUpstreamTest, AddBytesSentCallbackForCoverage) {
  this->setupUpstream();
  this->upstream_->addBytesSentCallback([&](uint64_t) { return true; });
}

TEST_P(HttpUpstreamTest, DownstreamDisconnect) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(this->callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TEST_P(HttpUpstreamTest, UpstreamReset) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(this->callbacks_, onEvent(_));
  this->upstream_->onResetStream(Http::StreamResetReason::ConnectionTermination, "");
}

TEST_P(HttpUpstreamTest, UpstreamWatermarks) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onAboveWriteBufferHighWatermark());
  this->upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(this->callbacks_, onBelowWriteBufferLowWatermark());
  this->upstream_->onBelowWriteBufferLowWatermark();
}

class MockHttpConnPoolCallbacks : public HttpConnPool::Callbacks {
public:
  MOCK_METHOD(void, onSuccess, (Http::RequestEncoder * request_encoder));
  MOCK_METHOD(void, onFailure, ());
};

TEST_P(HttpUpstreamTest, DownstreamDisconnectBeforeConnectResponse) {
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure());
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TEST_P(HttpUpstreamTest, OnSuccessCalledOnValidResponse) {
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure()).Times(0);
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_P(HttpUpstreamTest, OnFailureCalledOnInvalidResponse) {
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure());
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_)).Times(0);
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "404"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_P(HttpUpstreamTest, DumpsResponseDecoderWithoutAllocatingMemory) {
  std::array<char, 256> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  this->setupUpstream();

  Memory::TestUtil::MemoryTest memory_test;
  this->upstream_->responseDecoder().dumpState(ostream, 1);
  EXPECT_EQ(memory_test.consumedBytes(), 0);
  EXPECT_THAT(ostream.contents(), EndsWith("has not implemented dumpState\n"));
}

TEST_P(HttpUpstreamTest, UpstreamTrailersMarksDoneReading) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(_)).Times(0);
  this->upstream_->doneWriting();
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  this->upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

TEST_P(HttpUpstreamTest, UpstreamTrailersPropagateFinDownstream) {
  setupUpstream();
  EXPECT_CALL(encoder_.stream_, resetStream(_)).Times(0);
  upstream_->doneWriting();
  EXPECT_CALL(callbacks_, onUpstreamData(BufferStringEqual(""), true));
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

TEST_P(HttpUpstreamTest, UpstreamTrailersDontPropagateFinDownstreamWhenFeatureDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.tcp_tunneling_send_downstream_fin_on_upstream_trailers",
        "false"}});
  setupUpstream();
  EXPECT_CALL(encoder_.stream_, resetStream(_)).Times(0);
  upstream_->doneWriting();
  EXPECT_CALL(callbacks_, onUpstreamData(_, _)).Times(0);
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

class HttpUpstreamRequestEncoderTest : public testing::TestWithParam<Http::CodecType> {
public:
  HttpUpstreamRequestEncoderTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(this->encoder_, enableTcpTunneling()).Times(AnyNumber());

    if (GetParam() == Http::CodecType::HTTP1) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
      is_http2_ = false;
    }
    tcp_proxy_.mutable_tunneling_config()->set_hostname("default.host.com:443");
  }

  void setupUpstream() {
    config_ = std::make_unique<TunnelingConfigHelperImpl>(scope_, tcp_proxy_, context_);
    upstream_ = std::make_unique<HttpUpstream>(callbacks_, *this->config_,
                                               this->downstream_stream_info_, GetParam());
  }

  void populateMetadata(envoy::config::core::v3::Metadata& metadata, const std::string& ns,
                        const std::string& key, const std::string& value) {
    ProtobufWkt::Struct struct_obj;
    auto& fields_map = *struct_obj.mutable_fields();
    fields_map[key] = ValueUtil::stringValue(value);
    (*metadata.mutable_filter_metadata())[ns] = struct_obj;
  }

  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  std::unique_ptr<HttpUpstream> upstream_;
  TcpProxy tcp_proxy_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  std::unique_ptr<TunnelingConfigHelper> config_;
  bool is_http2_ = true;
};

INSTANTIATE_TEST_SUITE_P(H1H2H3Codecs, HttpUpstreamRequestEncoderTest,
                         ::testing::Values(Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                                           Http::CodecType::HTTP3));

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoder) {
  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderUsePost) {
  this->tcp_proxy_.mutable_tunneling_config()->set_use_post(true);
  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "POST"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
      {Http::Headers::get().Path, "/"},
  });

  if (this->is_http2_) {
    expected_headers->addReference(Http::Headers::get().Scheme,
                                   Http::Headers::get().SchemeValues.Http);
  }

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderUsePostWithCustomPath) {
  this->tcp_proxy_.mutable_tunneling_config()->set_use_post(true);
  this->tcp_proxy_.mutable_tunneling_config()->set_post_path("/test");
  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "POST"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
      {Http::Headers::get().Path, "/test"},
  });

  if (this->is_http2_) {
    expected_headers->addReference(Http::Headers::get().Scheme,
                                   Http::Headers::get().SchemeValues.Http);
  }

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderConnectWithCustomPath) {
  this->tcp_proxy_.mutable_tunneling_config()->set_use_post(false);
  this->tcp_proxy_.mutable_tunneling_config()->set_post_path("/test");
  EXPECT_THROW_WITH_MESSAGE(this->setupUpstream(), EnvoyException,
                            "Can't set a post path when POST method isn't used");
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderHeaders) {
  auto* header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("header0");
  hdr->set_value("value0");

  header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("header1");
  hdr->set_value("value1");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("header1");
  hdr->set_value("value2");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  expected_headers->setCopy(Http::LowerCaseString("header0"), "value0");
  expected_headers->addCopy(Http::LowerCaseString("header1"), "value1");
  expected_headers->addCopy(Http::LowerCaseString("header1"), "value2");

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, ConfigReuse) {
  auto* header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("key");
  hdr->set_value("value1");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("key");
  hdr->set_value("value2");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  expected_headers->setCopy(Http::LowerCaseString("key"), "value1");
  expected_headers->addCopy(Http::LowerCaseString("key"), "value2");

  expected_headers->setCopy(Http::LowerCaseString("key"), "value1");
  expected_headers->addCopy(Http::LowerCaseString("key"), "value2");

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);

  Http::MockRequestEncoder another_encoder;
  auto another_upstream = std::make_unique<HttpUpstream>(this->callbacks_, *this->config_,
                                                         this->downstream_stream_info_, GetParam());
  EXPECT_CALL(another_encoder, getStream()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, http1StreamEncoderOptions()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, enableTcpTunneling()).Times(AnyNumber());
  if (GetParam() == Http::CodecType::HTTP1) {
    ON_CALL(another_encoder, http1StreamEncoderOptions())
        .WillByDefault(
            Return(Http::Http1StreamEncoderOptionsOptRef(this->stream_encoder_options_)));
  }
  EXPECT_CALL(another_encoder, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  another_upstream->setRequestEncoder(another_encoder, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderHeadersWithDownstreamInfo) {
  auto* header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("header0");
  hdr->set_value("value0");

  header = this->tcp_proxy_.mutable_tunneling_config()->add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("downstream_local_port");
  hdr->set_value("%DOWNSTREAM_LOCAL_PORT%");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  expected_headers->setCopy(Http::LowerCaseString("header0"), "value0");
  expected_headers->addCopy(Http::LowerCaseString("downstream_local_port"), "80");
  auto ip_versions = TestEnvironment::getIpVersionsForTest();
  ASSERT_FALSE(ip_versions.empty());

  auto ip_port = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(ip_versions[0]), 80);
  Network::ConnectionInfoSetterImpl connection_info(ip_port, ip_port);
  EXPECT_CALL(this->downstream_stream_info_, downstreamAddressProvider)
      .WillRepeatedly(testing::ReturnRef(connection_info));
  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest,
       RequestEncoderHostnameWithDownstreamInfoRequestedServerName) {
  this->tcp_proxy_.mutable_tunneling_config()->set_hostname("%REQUESTED_SERVER_NAME%:443");
  this->setupUpstream();

  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, "www.google.com:443"},
  });

  auto ip_versions = TestEnvironment::getIpVersionsForTest();
  ASSERT_FALSE(ip_versions.empty());

  auto ip_port = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(ip_versions[0]), 80);
  Network::ConnectionInfoSetterImpl connection_info(ip_port, ip_port);
  connection_info.setRequestedServerName("www.google.com");
  EXPECT_CALL(this->downstream_stream_info_, downstreamAddressProvider)
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(connection_info));
  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TEST_P(HttpUpstreamRequestEncoderTest, RequestEncoderHostnameWithDownstreamInfoDynamicMetadata) {
  this->tcp_proxy_.mutable_tunneling_config()->set_hostname(
      "%DYNAMIC_METADATA(tunnel:address)%:443");
  this->setupUpstream();

  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, "www.google.com:443"},
  });

  auto ip_versions = TestEnvironment::getIpVersionsForTest();
  ASSERT_FALSE(ip_versions.empty());

  envoy::config::core::v3::Metadata metadata;
  this->populateMetadata(metadata, "tunnel", "address", "www.google.com");

  EXPECT_CALL(testing::Const(this->downstream_stream_info_), dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

class CombinedUpstreamTest : public testing::Test {
public:
  CombinedUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(encoder_, enableTcpTunneling()).Times(AnyNumber());
    EXPECT_CALL(stream_encoder_options_, enableHalfClose()).Times(AnyNumber());
    tcp_proxy_.mutable_tunneling_config()->set_hostname("default.host.com:443");
  }

  void setup() {
    tunnel_config_ = std::make_unique<TunnelingConfigHelperImpl>(scope_, tcp_proxy_, context_);
    conn_pool_ = std::make_unique<HttpConnPool>(cluster_, &lb_context_, *tunnel_config_, callbacks_,
                                                decoder_callbacks_, Http::CodecType::HTTP2,
                                                downstream_stream_info_);
    upstream_ = std::make_unique<CombinedUpstream>(*conn_pool_, callbacks_, decoder_callbacks_,
                                                   *tunnel_config_, downstream_stream_info_);
    auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
    std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
    config_ = std::make_shared<Config>(tcp_proxy_, factory_context_);
    filter_ =
        std::make_unique<Filter>(config_, factory_context_.serverFactoryContext().clusterManager());
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    auto mock_upst = std::make_unique<NiceMock<Router::MockUpstreamRequest>>(
        *upstream_, std::move(generic_conn_pool));
    mock_router_upstream_request_ = mock_upst.get();
    upstream_->setRouterUpstreamRequest(std::move(mock_upst));
    EXPECT_CALL(*mock_router_upstream_request_, acceptHeadersFromRouter(false));
    EXPECT_NO_THROW(tunnel_config_->serverFactoryContext());
    upstream_->newStream(*filter_);
  }

  Router::MockUpstreamRequest* mock_router_upstream_request_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;

  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  TcpProxy tcp_proxy_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_;
  std::unique_ptr<HttpConnPool> conn_pool_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  std::unique_ptr<TunnelingConfigHelper> tunnel_config_;
  std::unique_ptr<CombinedUpstream> upstream_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};
TEST_F(CombinedUpstreamTest, RouterFilterInterface) {
  this->setup();
  EXPECT_EQ(this->upstream_->startUpstreamSecureTransport(), false);
  EXPECT_EQ(this->upstream_->getUpstreamConnectionSslInfo(), nullptr);
  auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
  auto mock_upst = std::make_unique<NiceMock<Router::MockUpstreamRequest>>(
      *this->upstream_, std::move(generic_conn_pool));
  EXPECT_NO_THROW(this->upstream_->onUpstream1xxHeaders(nullptr, *mock_upst.get()));
  EXPECT_NO_THROW(this->upstream_->onUpstreamMetadata(nullptr));
  EXPECT_NO_THROW(this->upstream_->onPerTryTimeout(*mock_upst.get()));
  EXPECT_NO_THROW(this->upstream_->onPerTryIdleTimeout(*mock_upst.get()));
  EXPECT_NO_THROW(this->upstream_->onStreamMaxDurationReached(*mock_upst.get()));
  EXPECT_EQ(this->upstream_->dynamicMaxStreamDuration(), absl::nullopt);
  EXPECT_EQ(this->upstream_->downstreamTrailers(), nullptr);
  EXPECT_EQ(this->upstream_->downstreamResponseStarted(), false);
  EXPECT_EQ(this->upstream_->downstreamEndStream(), false);
  EXPECT_EQ(this->upstream_->attemptCount(), 0);
}

TEST_F(CombinedUpstreamTest, WriteUpstream) {
  this->setup();
  EXPECT_CALL(*this->mock_router_upstream_request_,
              acceptDataFromRouter(BufferStringEqual("foo"), false /*end_stream*/));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->encodeData(buffer1, false);

  EXPECT_CALL(*this->mock_router_upstream_request_,
              acceptDataFromRouter(BufferStringEqual("bar"), true /*end_stream*/));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->encodeData(buffer2, true);

  // New upstream with no encoder.
  this->upstream_ = std::make_unique<CombinedUpstream>(*conn_pool_, callbacks_, decoder_callbacks_,
                                                       *tunnel_config_, downstream_stream_info_);
  this->upstream_->encodeData(buffer2, true);
}

TEST_F(CombinedUpstreamTest, WriteDownstream) {
  this->setup();
  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->responseDecoder().decodeData(buffer1, false);

  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->responseDecoder().decodeData(buffer2, true);
}

TEST_F(CombinedUpstreamTest, InvalidUpgradeWithEarlyFin) {
  this->setup();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TEST_F(CombinedUpstreamTest, InvalidUpgradeWithNon200) {
  this->setup();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "301"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_F(CombinedUpstreamTest, ReadDisable) {
  this->setup();
  EXPECT_TRUE(this->upstream_->readDisable(true));

  EXPECT_TRUE(this->upstream_->readDisable(false));

  // New upstream with no encoder.
  this->upstream_ = std::make_unique<CombinedUpstream>(*conn_pool_, callbacks_, decoder_callbacks_,
                                                       *tunnel_config_, downstream_stream_info_);
  EXPECT_FALSE(this->upstream_->readDisable(true));
}

TEST_F(CombinedUpstreamTest, AddBytesSentCallbackForCoverage) {
  this->setup();
  this->upstream_->addBytesSentCallback([&](uint64_t) { return true; });
}

TEST_F(CombinedUpstreamTest, DownstreamDisconnect) {
  this->setup();
  EXPECT_CALL(this->callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TEST_F(CombinedUpstreamTest, OnSuccessCalledOnValidResponse) {
  this->setup();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure()).Times(0);
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_F(CombinedUpstreamTest, OnFailureCalledOnInvalidResponse) {
  this->setup();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure());
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_)).Times(0);
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "404"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_F(CombinedUpstreamTest, DumpsResponseDecoderWithoutAllocatingMemory) {
  std::array<char, 256> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  this->setup();

  Memory::TestUtil::MemoryTest memory_test;
  this->upstream_->responseDecoder().dumpState(ostream, 1);
  EXPECT_EQ(memory_test.consumedBytes(), 0);
  EXPECT_THAT(ostream.contents(), EndsWith("has not implemented dumpState\n"));
}
TEST_F(CombinedUpstreamTest, UpstreamTrailersMarksDoneReading) {
  this->setup();
  this->upstream_->doneWriting();
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  this->upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

TEST_F(CombinedUpstreamTest, UpstreamTrailersDontPropagateFinDownstreamWhenFeatureDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.tcp_tunneling_send_downstream_fin_on_upstream_trailers",
        "false"}});
  this->setup();
  upstream_->doneWriting();
  EXPECT_CALL(callbacks_, onUpstreamData(_, _)).Times(0);
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}
} // namespace
} // namespace TcpProxy
} // namespace Envoy
