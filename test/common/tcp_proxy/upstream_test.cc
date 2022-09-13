#include <memory>

#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/tcp_proxy/upstream.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
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
using envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

template <typename T> class HttpUpstreamTest : public testing::Test {
public:
  HttpUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, encodeHeaders(_, false));
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    EXPECT_CALL(encoder_, enableTcpTunneling()).Times(AnyNumber());
    if (typeid(T) == typeid(Http1Upstream)) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
    }
    EXPECT_CALL(stream_encoder_options_, enableHalfClose()).Times(AnyNumber());
    config_message_.set_hostname("default.host.com:443");
  }

  void setupUpstream() {
    config_ = std::make_unique<TunnelingConfigHelperImpl>(config_message_, context_);
    upstream_ = std::make_unique<T>(callbacks_, *this->config_, downstream_stream_info_);
    upstream_->setRequestEncoder(encoder_, true);
  }

  NiceMock<StreamInfo::MockStreamInfo> downstream_stream_info_;
  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  TcpProxy_TunnelingConfig config_message_;
  std::unique_ptr<TunnelingConfigHelper> config_;
  std::unique_ptr<HttpUpstream> upstream_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

using testing::Types;

using Implementations = Types<Http1Upstream, Http2Upstream>;

TYPED_TEST_SUITE(HttpUpstreamTest, Implementations);

TYPED_TEST(HttpUpstreamTest, WriteUpstream) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->encodeData(buffer1, false);

  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->encodeData(buffer2, true);

  // New upstream with no encoder.
  this->upstream_ =
      std::make_unique<TypeParam>(this->callbacks_, *this->config_, this->downstream_stream_info_);
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
  EXPECT_CALL(this->encoder_.stream_, readDisable(true));
  EXPECT_TRUE(this->upstream_->readDisable(true));

  EXPECT_CALL(this->encoder_.stream_, readDisable(false));
  EXPECT_TRUE(this->upstream_->readDisable(false));

  // New upstream with no encoder.
  this->upstream_ =
      std::make_unique<TypeParam>(this->callbacks_, *this->config_, this->downstream_stream_info_);
  EXPECT_FALSE(this->upstream_->readDisable(true));
}

TYPED_TEST(HttpUpstreamTest, AddBytesSentCallbackForCoverage) {
  this->setupUpstream();
  this->upstream_->addBytesSentCallback([&](uint64_t) { return true; });
}

TYPED_TEST(HttpUpstreamTest, DownstreamDisconnect) {
  this->setupUpstream();
  EXPECT_CALL(this->encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
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
  MOCK_METHOD(void, onSuccess, (Http::RequestEncoder & request_encoder));
  MOCK_METHOD(void, onFailure, ());
};

TYPED_TEST(HttpUpstreamTest, DownstreamDisconnectBeforeConnectResponse) {
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
    config_message_.set_hostname("default.host.com:443");
  }

  void setupUpstream() {
    config_ = std::make_unique<TunnelingConfigHelperImpl>(config_message_, context_);
    upstream_ = std::make_unique<T>(callbacks_, *this->config_, this->downstream_stream_info_);
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
  TcpProxy_TunnelingConfig config_message_;
  std::unique_ptr<TunnelingConfigHelper> config_;
  bool is_http2_ = true;
};

TYPED_TEST_SUITE(HttpUpstreamRequestEncoderTest, Implementations);

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderOld) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_rfc_connect", "false"}});

  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  if (this->is_http2_) {
    expected_headers->setReferenceKey(Http::Headers::get().Path, "/");
    expected_headers->setReferenceKey(Http::Headers::get().Scheme,
                                      Http::Headers::get().SchemeValues.Http);
    expected_headers->setReferenceKey(Http::Headers::get().Protocol,
                                      Http::Headers::get().ProtocolValues.Bytestream);
  }

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoder) {
  this->setupUpstream();
  std::unique_ptr<Http::RequestHeaderMapImpl> expected_headers;
  expected_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, "CONNECT"},
      {Http::Headers::get().Host, this->config_->host(this->downstream_stream_info_)},
  });

  EXPECT_CALL(this->encoder_, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  this->upstream_->setRequestEncoder(this->encoder_, false);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderUsePost) {
  this->config_message_.set_use_post(true);
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

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderHeaders) {
  auto* header = this->config_message_.add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("header0");
  hdr->set_value("value0");

  header = this->config_message_.add_headers_to_add();
  hdr = header->mutable_header();
  hdr->set_key("header1");
  hdr->set_value("value1");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  header = this->config_message_.add_headers_to_add();
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

TYPED_TEST(HttpUpstreamRequestEncoderTest, ConfigReuse) {
  auto* header = this->config_message_.add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("key");
  hdr->set_value("value1");
  header->set_append_action(envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  header = this->config_message_.add_headers_to_add();
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
  auto another_upstream =
      std::make_unique<TypeParam>(this->callbacks_, *this->config_, this->downstream_stream_info_);
  EXPECT_CALL(another_encoder, getStream()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, http1StreamEncoderOptions()).Times(AnyNumber());
  EXPECT_CALL(another_encoder, enableTcpTunneling()).Times(AnyNumber());
  if (typeid(TypeParam) == typeid(Http1Upstream)) {
    ON_CALL(another_encoder, http1StreamEncoderOptions())
        .WillByDefault(
            Return(Http::Http1StreamEncoderOptionsOptRef(this->stream_encoder_options_)));
  }
  EXPECT_CALL(another_encoder, encodeHeaders(HeaderMapEqualRef(expected_headers.get()), false));
  another_upstream->setRequestEncoder(another_encoder, false);
}

TYPED_TEST(HttpUpstreamRequestEncoderTest, RequestEncoderHeadersWithDownstreamInfo) {
  auto* header = this->config_message_.add_headers_to_add();
  auto* hdr = header->mutable_header();
  hdr->set_key("header0");
  hdr->set_value("value0");

  header = this->config_message_.add_headers_to_add();
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

TYPED_TEST(HttpUpstreamRequestEncoderTest,
           RequestEncoderHostnameWithDownstreamInfoRequestedServerName) {
  this->config_message_.set_hostname("%REQUESTED_SERVER_NAME%:443");
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

TYPED_TEST(HttpUpstreamRequestEncoderTest,
           RequestEncoderHostnameWithDownstreamInfoDynamicMetadata) {
  this->config_message_.set_hostname("%DYNAMIC_METADATA(tunnel:address)%:443");
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
} // namespace
} // namespace TcpProxy
} // namespace Envoy
