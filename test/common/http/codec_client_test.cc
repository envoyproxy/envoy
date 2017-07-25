#include <memory>

#include "common/buffer/buffer_impl.h"
#include "common/http/codec_client.h"
#include "common/http/exception.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Invoke;
using testing::NiceMock;
using testing::Pointee;
using testing::Ref;
using testing::Return;
using testing::SaveArg;
using testing::Throw;
using testing::_;

namespace Http {

class CodecClientTest : public testing::Test {
public:
  CodecClientTest() {
    connection_ = new NiceMock<Network::MockClientConnection>();

    EXPECT_CALL(*connection_, addConnectionCallbacks(_)).WillOnce(SaveArgAddress(&connection_cb_));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, addReadFilter(_))
        .WillOnce(
            Invoke([this](Network::ReadFilterSharedPtr filter) -> void { filter_ = filter; }));

    codec_ = new Http::MockClientConnection();

    Network::ClientConnectionPtr connection{connection_};
    client_.reset(new CodecClientForTest(std::move(connection), codec_, nullptr, host_));
  }

  ~CodecClientTest() { EXPECT_EQ(0U, client_->numActiveRequests()); }

  Event::MockDispatcher dispatcher_;
  Network::MockClientConnection* connection_;
  Http::MockClientConnection* codec_;
  std::unique_ptr<CodecClientForTest> client_;
  Network::ConnectionCallbacks* connection_cb_;
  Network::ReadFilterSharedPtr filter_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_{new Upstream::HostDescriptionImpl(
      cluster_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, "")};
};

TEST_F(CodecClientTest, BasicHeaderOnlyResponse) {
  Http::StreamDecoder* inner_decoder;
  NiceMock<Http::MockStreamEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder) -> Http::StreamEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockStreamDecoder outer_decoder;
  client_->newStream(outer_decoder);

  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(outer_decoder, decodeHeaders_(Pointee(Ref(*response_headers)), true));
  inner_decoder->decodeHeaders(std::move(response_headers), true);
}

TEST_F(CodecClientTest, BasicResponseWithBody) {
  Http::StreamDecoder* inner_decoder;
  NiceMock<Http::MockStreamEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder) -> Http::StreamEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockStreamDecoder outer_decoder;
  client_->newStream(outer_decoder);

  Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(outer_decoder, decodeHeaders_(Pointee(Ref(*response_headers)), false));
  inner_decoder->decodeHeaders(std::move(response_headers), false);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(outer_decoder, decodeData(Ref(buffer), true));
  inner_decoder->decodeData(buffer, true);
}

TEST_F(CodecClientTest, DisconnectBeforeHeaders) {
  Http::StreamDecoder* inner_decoder;
  NiceMock<Http::MockStreamEncoder> inner_encoder;
  EXPECT_CALL(*codec_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder) -> Http::StreamEncoder& {
        inner_decoder = &decoder;
        return inner_encoder;
      }));

  Http::MockStreamDecoder outer_decoder;
  Http::StreamEncoder& request_encoder = client_->newStream(outer_decoder);
  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);

  // When we get a remote close with an active request we should try to send zero bytes through
  // the codec.
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::ConnectionTermination));
  EXPECT_CALL(*codec_, dispatch(_));
  connection_cb_->onEvent(Network::ConnectionEvent::Connected);
  connection_cb_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(CodecClientTest, ProtocolError) {
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Throw(CodecProtocolException("protocol error")));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, 408Response) {
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([](Buffer::Instance&) -> void {
    Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "408"}}};
    throw PrematureResponseException(std::move(response_headers));
  }));

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data);

  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, PrematureResponse) {
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([](Buffer::Instance&) -> void {
    Http::HeaderMapPtr response_headers{new TestHeaderMapImpl{{":status", "200"}}};
    throw PrematureResponseException(std::move(response_headers));
  }));

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data;
  filter_->onData(data);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_protocol_error_.value());
}

TEST_F(CodecClientTest, WatermarkPassthrough) {
  EXPECT_CALL(*codec_, onAboveWriteBufferHighWatermark());
  connection_cb_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(*codec_, onBelowWriteBufferLowWatermark());
  connection_cb_->onBelowWriteBufferLowWatermark();
}

} // namespace Http
} // namespace Envoy
