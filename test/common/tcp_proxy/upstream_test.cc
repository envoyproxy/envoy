#include <memory>

#include "common/tcp_proxy/upstream.h"
#include "common/tcp_proxy/upstream_interface.h"

#include "test/common/tcp_proxy/mocks.h"
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

  Envoy::Http::MockRequestEncoder encoder_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockUpstreamCallbacks> callbacks_;
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
  Envoy::Http::ResponseHeaderMapPtr headers{
      new Envoy::Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  upstream_->responseDecoder().decodeHeaders(std::move(headers), true);
}

TEST_F(HttpUpstreamTest, InvalidUpgradeWithNon200) {
  EXPECT_CALL(callbacks_, onEvent(_));
  Envoy::Http::ResponseHeaderMapPtr headers{
      new Envoy::Http::TestResponseHeaderMapImpl{{":status", "301"}}};
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
  EXPECT_CALL(encoder_.stream_, resetStream(Envoy::Http::StreamResetReason::LocalReset));
  EXPECT_CALL(callbacks_, onEvent(_)).Times(0);
  EXPECT_TRUE(upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose) == nullptr);
}

TEST_F(HttpUpstreamTest, UpstreamReset) {
  EXPECT_CALL(encoder_.stream_, resetStream(_)).Times(0);
  EXPECT_CALL(callbacks_, onEvent(_));
  upstream_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
}

TEST_F(HttpUpstreamTest, UpstreamWatermarks) {
  EXPECT_CALL(callbacks_, onAboveWriteBufferHighWatermark());
  upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(callbacks_, onBelowWriteBufferLowWatermark());
  upstream_->onBelowWriteBufferLowWatermark();
}

class HttpGenericConnPoolTest : public testing::Test {
public:
  HttpGenericConnPoolTest() {
    http_conn_handle_ =
        std::make_unique<HttpGenericConnPool>(&cancellable_, generic_pool_callbacks_);
  }
  std::unique_ptr<HttpGenericConnPool> http_conn_handle_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  Envoy::TcpProxy::MockGenericUpstreamPoolCallbacks generic_pool_callbacks_;
};

TEST_F(HttpGenericConnPoolTest, DestroyOnAvailableCanncellable) {
  EXPECT_CALL(cancellable_, cancel(ConnectionPool::CancelPolicy::Default)).Times(1);
  http_conn_handle_.reset();
}

TEST_F(HttpGenericConnPoolTest, DestroyAfterComplete) {
  EXPECT_CALL(cancellable_, cancel(_)).Times(0);
  http_conn_handle_->complete();
  http_conn_handle_.reset();
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
