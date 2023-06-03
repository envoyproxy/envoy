#include <asm-generic/errno-base.h>
#include <memory>

#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/tcp_proxy/upstream.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/router/upstream_request.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
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

template <typename T> class HttpUpstreamTest : public testing::Test {
public:
  HttpUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(encoder_, enableTcpTunneling()).Times(AnyNumber());
    if (typeid(T) != typeid(CombinedUpstream)) {
      EXPECT_CALL(encoder_, encodeHeaders(_, false));
    }
    if (typeid(T) == typeid(Http1Upstream)) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
    }
    EXPECT_CALL(stream_encoder_options_, enableHalfClose()).Times(AnyNumber());
    tcp_proxy_.mutable_tunneling_config()->set_hostname("default.host.com:443");
  }

  void expectCallOnEncodeData(std::string data_string, bool end_stream) {
    if (typeid(T) == typeid(CombinedUpstream)) {
      EXPECT_CALL(*this->mock_router_upstream_request_,
                  acceptDataFromRouter(BufferStringEqual(data_string), end_stream));
    } else {
      EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual(data_string), end_stream));
    }
  }

  void expectCallOnReadDisable(bool disable) {
    if (typeid(T) == typeid(CombinedUpstream)) {
      disable ? EXPECT_CALL(*this->mock_router_upstream_request_, onAboveWriteBufferHighWatermark())
                    .Times(1)
              : EXPECT_CALL(*this->mock_router_upstream_request_, onAboveWriteBufferHighWatermark())
                    .Times(0);
    } else {
      EXPECT_CALL(this->encoder_.stream_, readDisable(disable));
    }
  }

  void expectCallOnResetStream() {
    if (typeid(T) == typeid(CombinedUpstream)) {
      EXPECT_CALL(*this->mock_router_upstream_request_, resetStream()).Times(AnyNumber());
    } else {
      EXPECT_CALL(this->encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
    }
  }

  void setupUpstream() {
    route_ = std::make_unique<HttpConnPool::RouteImpl>(cluster_, &lb_context_);
    tunnel_config_ = std::make_unique<TunnelingConfigHelperImpl>(scope_, tcp_proxy_, context_);
    conn_pool_ = std::make_unique<HttpConnPool>(cluster_, &lb_context_, *tunnel_config_, callbacks_,
                                                decoder_callbacks_, Http::CodecType::HTTP2,
                                                downstream_stream_info_);
    upstream_ = std::make_unique<T>(*conn_pool_, callbacks_, decoder_callbacks_, *route_,
                                    *tunnel_config_, downstream_stream_info_);
    if (typeid(T) == typeid(CombinedUpstream)) {
      auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
      std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
      config_ = std::make_shared<Config>(tcp_proxy_, factory_context_);
      filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      auto mock_upst = std::make_unique<NiceMock<Router::MockUpstreamRequest>>(
          *upstream_, std::move(generic_conn_pool));
      mock_router_upstream_request_ = mock_upst.get();
      upstream_->setRouterUpstreamRequest(std::move(mock_upst));
      EXPECT_CALL(*mock_router_upstream_request_, acceptHeadersFromRouter(false));
      upstream_->newStream(*filter_);
    } else {
      upstream_->setRequestEncoder(encoder_, true);
    }
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
  Router::MockGenericConnPool* generic_conn_pool_{};
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_;
  std::unique_ptr<HttpConnPool> conn_pool_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  std::unique_ptr<TunnelingConfigHelper> tunnel_config_;
  std::unique_ptr<HttpConnPool::RouteImpl> route_;
  std::unique_ptr<HttpUpstream> upstream_;
};

using testing::Types;

using Implementations = Types<Http1Upstream, Http2Upstream, CombinedUpstream>;

TYPED_TEST_SUITE(HttpUpstreamTest, Implementations);

TYPED_TEST(HttpUpstreamTest, WriteUpstream) {
  this->setupUpstream();
  this->expectCallOnEncodeData("foo", false);
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->encodeData(buffer1, false);
  this->expectCallOnEncodeData("bar", true);
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->encodeData(buffer2, true);

  // New upstream with no encoder.
  auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
  this->upstream_ = std::make_unique<TypeParam>(
      *this->conn_pool_, this->callbacks_, this->decoder_callbacks_, *this->route_,
      *this->tunnel_config_, this->downstream_stream_info_);
  this->upstream_->encodeData(buffer2, true);
}

TYPED_TEST(HttpUpstreamTest, WriteDownstream) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->responseDecoder().decodeData(buffer1, false);

  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->responseDecoder().decodeData(buffer2, true);
}

TYPED_TEST(HttpUpstreamTest, InvalidUpgradeWithEarlyFin) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TYPED_TEST(HttpUpstreamTest, InvalidUpgradeWithNon200) {
  this->setupUpstream();
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "301"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TYPED_TEST(HttpUpstreamTest, ReadDisable) {
  this->setupUpstream();
  this->expectCallOnReadDisable(true);
  EXPECT_TRUE(this->upstream_->readDisable(true));

  this->expectCallOnReadDisable(false);
  EXPECT_TRUE(this->upstream_->readDisable(false));

  // New upstream with no encoder.
  auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
  this->upstream_ = std::make_unique<TypeParam>(
      *this->conn_pool_, this->callbacks_, this->decoder_callbacks_, *this->route_,
      *this->tunnel_config_, this->downstream_stream_info_);
  EXPECT_FALSE(this->upstream_->readDisable(true));
}

TYPED_TEST(HttpUpstreamTest, AddBytesSentCallbackForCoverage) {
  this->setupUpstream();
  this->upstream_->addBytesSentCallback([&](uint64_t) { return true; });
}

TYPED_TEST(HttpUpstreamTest, DownstreamDisconnect) {
  this->setupUpstream();
  this->expectCallOnResetStream();
  EXPECT_CALL(this->callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TYPED_TEST(HttpUpstreamTest, UpstreamReset) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(this->callbacks_, onEvent(_));
  this->upstream_->onResetStream(Http::StreamResetReason::ConnectionTermination, "");
}

TYPED_TEST(HttpUpstreamTest, UpstreamWatermarks) {
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

TYPED_TEST(HttpUpstreamTest, DownstreamDisconnectBeforeConnectResponse) {
  if (std::is_same<TypeParam, CombinedUpstream>::value) {
    // CombinedUpstream sets connPoolCallbacks_ when onUpstreamHostSelected is called
    // by Router::UpstreamRequest. Hence this test is not applicable.
    return;
  }
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure());
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TYPED_TEST(HttpUpstreamTest, OnSuccessCalledOnValidResponse) {
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure()).Times(0);
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TYPED_TEST(HttpUpstreamTest, OnFailureCalledOnInvalidResponse) {
  this->setupUpstream();
  auto conn_pool_callbacks = std::make_unique<MockHttpConnPoolCallbacks>();
  auto conn_pool_callbacks_raw = conn_pool_callbacks.get();
  this->upstream_->setConnPoolCallbacks(std::move(conn_pool_callbacks));
  EXPECT_CALL(*conn_pool_callbacks_raw, onFailure());
  EXPECT_CALL(*conn_pool_callbacks_raw, onSuccess(_)).Times(0);
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "404"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TYPED_TEST(HttpUpstreamTest, DumpsResponseDecoderWithoutAllocatingMemory) {
  std::array<char, 256> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  this->setupUpstream();

  Stats::TestUtil::MemoryTest memory_test;
  this->upstream_->responseDecoder().dumpState(ostream, 1);
  EXPECT_EQ(memory_test.consumedBytes(), 0);
  EXPECT_THAT(ostream.contents(), EndsWith("has not implemented dumpState\n"));
}

TYPED_TEST(HttpUpstreamTest, UpstreamTrailersMarksDoneReading) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(_)).Times(0);
  this->upstream_->doneWriting();
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  this->upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

TYPED_TEST(HttpUpstreamTest, UpstreamTrailersNotMarksDoneReadingWhenFeatureDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.finish_reading_on_decode_trailers", "false"}});
  this->setupUpstream();
  this->expectCallOnResetStream();
  this->upstream_->doneWriting();
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{{"key", "value"}}};
  this->upstream_->responseDecoder().decodeTrailers(std::move(trailers));
}

template <typename T> class HttpUpstreamRequestEncoderTest : public testing::Test {
public:
  HttpUpstreamRequestEncoderTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(this->encoder_, enableTcpTunneling()).Times(AnyNumber());

    if (typeid(T) == typeid(Http1Upstream)) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
      is_http2_ = false;
    }
    tcp_proxy_.mutable_tunneling_config()->set_hostname("default.host.com:443");
  }

  void setupUpstream() {
    route_ = std::make_unique<HttpConnPool::RouteImpl>(cluster_, &lb_context_);
    config_ = std::make_unique<TunnelingConfigHelperImpl>(scope_, tcp_proxy_, context_);
    conn_pool_ = std::make_unique<HttpConnPool>(cluster_, &lb_context_, *config_, callbacks_,
                                                decoder_callbacks_, Http::CodecType::HTTP2,
                                                downstream_stream_info_);
    upstream_ = std::make_unique<T>(*conn_pool_, callbacks_, decoder_callbacks_, *route_, *config_,
                                    downstream_stream_info_);
    if (typeid(T) == typeid(CombinedUpstream)) {
      auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
      std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
      filter_config_ = std::make_shared<Config>(tcp_proxy_, factory_context_);
      filter_ = std::make_unique<Filter>(filter_config_, factory_context_.cluster_manager_);
      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      auto mock_upst = std::make_unique<NiceMock<Router::MockUpstreamRequest>>(
          *upstream_, std::move(generic_conn_pool));
      mock_router_upstream_request_ = mock_upst.get();
      upstream_->setRouterUpstreamRequest(std::move(mock_upst));
    }
  }

  void setRequestEncoderAndVerifyHeaders(Http::RequestHeaderMapImpl* expected_headers,
                                         bool end_stream = false) {
    if (typeid(T) == typeid(CombinedUpstream)) {
      upstream_->newStream(*filter_);
      Router::RouterFilterInterface* router_filter =
          dynamic_cast<Router::RouterFilterInterface*>(this->upstream_.get());
      EXPECT_THAT(*router_filter->downstreamHeaders(), HeaderMapEqualRef(expected_headers));
    } else {
      upstream_->setRequestEncoder(encoder_, end_stream);
    }
  }

  void expectCallOnSetRequestEncoder(std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers,
                                     bool end_stream = false) {
    if (typeid(T) == typeid(CombinedUpstream)) {
      EXPECT_CALL(*mock_router_upstream_request_, acceptHeadersFromRouter(end_stream));
    } else {
      EXPECT_CALL(encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), end_stream));
    }
  }

  void populateMetadata(envoy::config::core::v3::Metadata& metadata, const std::string& ns,
                        const std::string& key, const std::string& value) {
    ProtobufWkt::Struct struct_obj;
    auto& fields_map = *struct_obj.mutable_fields();
    fields_map[key] = ValueUtil::stringValue(value);
    (*metadata.mutable_filter_metadata())[ns] = struct_obj;
  }

  Router::MockUpstreamRequest* mock_router_upstream_request_{};
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr filter_config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;

  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  TcpProxy tcp_proxy_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::unique_ptr<TunnelingConfigHelper> config_;
  bool is_http2_ = true;
  std::unique_ptr<HttpConnPool> conn_pool_;
  std::unique_ptr<Router::GenericConnPool> generic_conn_pool_;
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_;
  NiceMock<Stats::MockStore> store_;
  Stats::MockScope& scope_{store_.mockScope()};
  std::unique_ptr<HttpConnPool::RouteImpl> route_;
  std::unique_ptr<HttpUpstream> upstream_;
};

TYPED_TEST_SUITE(HttpUpstreamRequestEncoderTest, Implementations);

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoder) {
  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderUsePost) {
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
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderUsePostWithCustomPath) {
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

  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderConnectWithCustomPath) {
  this->tcp_proxy_.mutable_tunneling_config()->set_use_post(false);
  this->tcp_proxy_.mutable_tunneling_config()->set_post_path("/test");
  EXPECT_THROW_WITH_MESSAGE(this->setupUpstream(), EnvoyException,
                            "Can't set a post path when POST method isn't used");
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderHeaders) {
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

  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, ConfigReuse) {
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
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);

  Http::MockRequestEncoder another_encoder;
  auto another_upstream =
      std::make_unique<TypeParam>(*this->conn_pool_, this->callbacks_, this->decoder_callbacks_,
                                  *this->route_, *this->config_, this->downstream_stream_info_);

  EXPECT_CALL(another_encoder, getStream()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, http1StreamEncoderOptions()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, enableTcpTunneling()).Times(AnyNumber());
  if (typeid(TypeParam) == typeid(Http1Upstream)) {
    ON_CALL(another_encoder, http1StreamEncoderOptions())
        .WillByDefault(
            Return(Http::Http1StreamEncoderOptionsOptRef(this->stream_encoder_options_)));
  }

  if (typeid(TypeParam) == typeid(CombinedUpstream)) {
    auto mock_conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
    std::unique_ptr<Router::GenericConnPool> generic_conn_pool = std::move(mock_conn_pool);
    auto mock_upst = std::make_unique<NiceMock<Router::MockUpstreamRequest>>(
        *another_upstream, std::move(generic_conn_pool));
    auto another_router_upstream_request = mock_upst.get();
    another_upstream->setRouterUpstreamRequest(std::move(mock_upst));
    EXPECT_CALL(*another_router_upstream_request, acceptHeadersFromRouter(false));
    another_upstream->newStream(*this->filter_);
    Router::RouterFilterInterface* router_filter =
        dynamic_cast<Router::RouterFilterInterface*>(another_upstream.get());
    EXPECT_THAT(*router_filter->downstreamHeaders(), HeaderMapEqualRef(tmp_expected_headers));
  } else {
    EXPECT_CALL(another_encoder, encodeHeaders(HeaderMapEqualRef(tmp_expected_headers), false));
    another_upstream->setRequestEncoder(another_encoder, false);
  }
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderHeadersWithDownstreamInfo) {
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
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest,
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
      .Times(AnyNumber())
      .WillRepeatedly(testing::ReturnRef(connection_info));
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest,
           RequestEncoderHostnameWithDownstreamInfoDynamicMetadata) {
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
  auto tmp_expected_headers = expected_headers.get();
  this->expectCallOnSetRequestEncoder(std::move(expected_headers), false);
  this->setRequestEncoderAndVerifyHeaders(tmp_expected_headers);
}
} // namespace
} // namespace TcpProxy
} // namespace Envoy
