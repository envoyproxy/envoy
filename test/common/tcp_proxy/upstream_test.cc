#include <memory>

#include "common/tcp_proxy/upstream.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/tcp/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;

namespace Envoy {
namespace TcpProxy {
namespace {

class HttpUpstreamTest : public testing::Test {
public:
  HttpUpstreamTest() {
    EXPECT_CALL(encoder_, getStream()).Times(AnyNumber());
    EXPECT_CALL(encoder_, encodeHeaders(_, false));
    upstream_ = std::make_unique<HttpUpstream>(callbacks_, hostname_);
    upstream_->setRequestEncoder(encoder_, true);
  }

  Http::MockRequestEncoder encoder_;
  NiceMock<Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
  std::unique_ptr<HttpUpstream> upstream_;
  std::string hostname_{"default.host.com"};
};

TEST_F(HttpUpstreamTest, WriteUpstream) {
  EXPECT_CALL(encoder_, encodeData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  upstream_->encodeData(buffer1, false);

  EXPECT_CALL(encoder_, encodeData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  upstream_->encodeData(buffer2, true);

  // New upstream with no encoder
  upstream_ = std::make_unique<HttpUpstream>(callbacks_, hostname_);
  upstream_->encodeData(buffer2, true);
}

TEST_F(HttpUpstreamTest, WriteDownstream) {
  EXPECT_CALL(callbacks_, onUpstreamData(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer1("foo");
  upstream_->responseDecoder().decodeData(buffer1, false);

  EXPECT_CALL(callbacks_, onUpstreamData(BufferStringEqual("bar"), true));
  Buffer::OwnedImpl buffer2("bar");
  upstream_->responseDecoder().decodeData(buffer2, true);
}

TEST_F(HttpUpstreamTest, InvalidUpgradeWithEarlyFin) {
  EXPECT_CALL(callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TEST_F(HttpUpstreamTest, InvalidUpgradeWithNon200) {
  EXPECT_CALL(callbacks_, onEvent(_));
  Http::ResponseHeaderMapPtr headers{new Http::TestResponseHeaderMapImpl{{":status", "301"}}};
  upstream_->responseDecoder().decodeHeaders(std::move(headers), false);
}

TEST_F(HttpUpstreamTest, ReadDisable) {
  EXPECT_CALL(encoder_.stream_, readDisable(true));
  EXPECT_TRUE(upstream_->readDisable(true));

  EXPECT_CALL(encoder_.stream_, readDisable(false));
  EXPECT_TRUE(upstream_->readDisable(false));

  // New upstream with no encoder
  upstream_ = std::make_unique<HttpUpstream>(callbacks_, hostname_);
  EXPECT_FALSE(upstream_->readDisable(true));
}

TEST_F(HttpUpstreamTest, AddBytesSentCallbackForCoverage) {
  upstream_->addBytesSentCallback([&](uint64_t) {});
}

TEST_F(HttpUpstreamTest, DownstreamDisconnect) {
  EXPECT_CALL(encoder_.stream_, resetStream(Http::StreamResetReason::LocalReset));
  EXPECT_CALL(callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TEST_F(HttpUpstreamTest, UpstreamReset) {
  EXPECT_CALL(encoder_.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, onEvent(_));
  upstream_->onResetStream(Http::StreamResetReason::ConnectionTermination, "");
}

TEST_F(HttpUpstreamTest, UpstreamWatermarks) {
  EXPECT_CALL(callbacks_, onAboveWriteBufferHighWatermark());
  upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(callbacks_, onBelowWriteBufferLowWatermark());
  upstream_->onBelowWriteBufferLowWatermark();
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
