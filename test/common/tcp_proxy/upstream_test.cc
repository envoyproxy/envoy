#include <memory>

#include "common/tcp_proxy/upstream.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/tcp/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace Envoy {
namespace TcpProxy {
namespace {

template <typename T> class HttpUpstreamTest : public testing::Test {
public:
  HttpUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, encodeHeaders(_, false));
    EXPECT_CALL(encoder_, http1StreamEncoderOptions()).Times(AnyNumber());
    if (typeid(T) == typeid(Http1Upstream)) {
      ON_CALL(encoder_, http1StreamEncoderOptions())
          .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options_)));
    }
    EXPECT_CALL(stream_encoder_options_, enableHalfClose()).Times(AnyNumber());
    upstream_ = std::make_unique<T>(callbacks_, hostname_);
    upstream_->setRequestEncoder(encoder_, true);
  }

  Http::MockRequestEncoder encoder_;
  Http::MockHttp1StreamEncoderOptions stream_encoder_options_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
  std::string hostname_{"default.host.com:443"};
};

using testing::Types;

typedef Types<Http1Upstream, Http2Upstream> Implementations;

TYPED_TEST_SUITE(HttpUpstreamTest, Implementations);

TYPED_TEST(HttpUpstreamTest, WriteUpstream) {
  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->encodeData(buffer1, false);

  EXPECT_CALL(this->encoder_, encodeData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->encodeData(buffer2, true);

  // New upstream with no encoder
  this->upstream_ = std::make_unique<TypeParam>(this->callbacks_, this->hostname_);
  this->upstream_->encodeData(buffer2, true);
}

TYPED_TEST(HttpUpstreamTest, WriteDownstream) {
  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  this->upstream_->responseDecoder().decodeData(buffer1, false);

  EXPECT_CALL(this->callbacks_, onUpstreamData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  this->upstream_->responseDecoder().decodeData(buffer2, true);
}

TYPED_TEST(HttpUpstreamTest, InvalidUpgradeWithEarlyFin) {
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TYPED_TEST(HttpUpstreamTest, InvalidUpgradeWithNon200) {
  EXPECT_CALL(this->callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "301"}}};
  this->upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TYPED_TEST(HttpUpstreamTest, ReadDisable) {
  EXPECT_CALL(this->encoder_.stream_, readDisable(true));
  EXPECT_TRUE(this->upstream_->readDisable(true));

  EXPECT_CALL(this->encoder_.stream_, readDisable(false));
  EXPECT_TRUE(this->upstream_->readDisable(false));

  // New upstream with no encoder
  this->upstream_ = std::make_unique<TypeParam>(this->callbacks_, this->hostname_);
  EXPECT_FALSE(this->upstream_->readDisable(true));
}

TYPED_TEST(HttpUpstreamTest, AddBytesSentCallbackForCoverage) {
  this->upstream_->addBytesSentCallback([&](uint64_t) { return true; });
}

TYPED_TEST(HttpUpstreamTest, DownstreamDisconnect) {
  EXPECT_CALL(this->encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(this->callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(this->upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TYPED_TEST(HttpUpstreamTest, UpstreamReset) {
  EXPECT_CALL(this->encoder_.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(this->callbacks_, onEvent(_));
  this->upstream_->onResetStream(Http::StreamResetReason::ConnectionTermination, "");
}

TYPED_TEST(HttpUpstreamTest, UpstreamWatermarks) {
  EXPECT_CALL(this->callbacks_, onAboveWriteBufferHighWatermark());
  this->upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(this->callbacks_, onBelowWriteBufferLowWatermark());
  this->upstream_->onBelowWriteBufferLowWatermark();
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
