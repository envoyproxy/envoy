#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/message_impl.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DecoderStateMachineTestBase {
public:
  DecoderStateMachineTestBase() = default;
  virtual ~DecoderStateMachineTestBase() { active_stream_.reset(); }

  void initHandler() {
    ON_CALL(delegate_, newStream(_, _))
        .WillByDefault(Invoke([this](MessageMetadataSharedPtr data,
                                     ContextSharedPtr ctx) -> ActiveStream* {
          this->active_stream_ = std::make_shared<NiceMock<MockActiveStream>>(handler_, data, ctx);
          return active_stream_.get();
        }));
  }

  void initProtocolDecoder(MessageType type, int32_t body_size) {
    ON_CALL(protocol_, decodeHeader(_, _))
        .WillByDefault(
            Invoke([=](Buffer::Instance&,
                       MessageMetadataSharedPtr metadata) -> std::pair<ContextSharedPtr, bool> {
              auto context = std::make_shared<ContextImpl>();
              context->set_header_size(16);
              context->set_body_size(body_size);
              metadata->setMessageType(type);

              return std::pair<ContextSharedPtr, bool>(context, true);
            }));
  }

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockDecoderStateMachineDelegate> delegate_;
  std::shared_ptr<NiceMock<MockActiveStream>> active_stream_;
  NiceMock<MockStreamHandler> handler_;
};

class DubboDecoderStateMachineTest : public DecoderStateMachineTestBase, public testing::Test {};

class DubboDecoderTest : public testing::Test {
public:
  DubboDecoderTest() = default;
  ~DubboDecoderTest() override = default;

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockStreamHandler> handler_;
  NiceMock<MockRequestDecoderCallbacks> request_callbacks_;
  NiceMock<MockResponseDecoderCallbacks> response_callbacks_;
};

TEST_F(DubboDecoderStateMachineTest, EmptyData) {
  EXPECT_CALL(protocol_, decodeHeader(_, _)).Times(1);
  EXPECT_CALL(delegate_, newStream(_, _)).Times(0);
  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);

  DecoderStateMachine dsm(protocol_, delegate_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DubboDecoderStateMachineTest, OnlyHaveHeaderData) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 1);

  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(protocol_, decodeData(_, _, _)).WillOnce(Return(false));

  Buffer::OwnedImpl buffer;
  DecoderStateMachine dsm(protocol_, delegate_);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DubboDecoderStateMachineTest, RequestMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 0);

  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(protocol_, decodeData(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(1);

  DecoderStateMachine dsm(protocol_, delegate_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);

  EXPECT_EQ(active_stream_->metadata_->message_type(), MessageType::Request);
}

TEST_F(DubboDecoderStateMachineTest, ResponseMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Response, 0);

  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(protocol_, decodeData(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(1);

  DecoderStateMachine dsm(protocol_, delegate_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);

  EXPECT_EQ(active_stream_->metadata_->message_type(), MessageType::Response);
}

TEST_F(DubboDecoderStateMachineTest, SerializeRpcInvocationException) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 0);

  EXPECT_CALL(delegate_, newStream(_, _)).Times(1);
  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(0);

  EXPECT_CALL(protocol_, decodeData(_, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, ContextSharedPtr, MessageMetadataSharedPtr) -> bool {
        throw EnvoyException(fmt::format("mock serialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, delegate_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock serialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnDecodeStreamData);
}

TEST_F(DubboDecoderStateMachineTest, SerializeRpcResultException) {
  initHandler();
  initProtocolDecoder(MessageType::Response, 0);

  EXPECT_CALL(delegate_, newStream(_, _)).Times(1);
  EXPECT_CALL(delegate_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(0);

  EXPECT_CALL(protocol_, decodeData(_, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, ContextSharedPtr, MessageMetadataSharedPtr) -> bool {
        throw EnvoyException(fmt::format("mock serialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, delegate_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock serialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnDecodeStreamData);
}

TEST_F(DubboDecoderStateMachineTest, ProtocolDecodeException) {
  EXPECT_CALL(delegate_, newStream(_, _)).Times(0);
  EXPECT_CALL(protocol_, decodeHeader(_, _))
      .WillOnce(Invoke(
          [](Buffer::Instance&, MessageMetadataSharedPtr) -> std::pair<ContextSharedPtr, bool> {
            throw EnvoyException(fmt::format("mock protocol decode exception"));
          }));

  DecoderStateMachine dsm(protocol_, delegate_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock protocol decode exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnDecodeStreamHeader);
}

TEST_F(DubboDecoderTest, NeedMoreDataForProtocolHeader) {
  EXPECT_CALL(request_callbacks_, newStream()).Times(0);
  EXPECT_CALL(protocol_, decodeHeader(_, _))
      .WillOnce(Invoke(
          [](Buffer::Instance&, MessageMetadataSharedPtr) -> std::pair<ContextSharedPtr, bool> {
            return std::pair<ContextSharedPtr, bool>(nullptr, false);
          }));

  RequestDecoder decoder(protocol_, request_callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DubboDecoderTest, NeedMoreDataForProtocolBody) {
  EXPECT_CALL(protocol_, decodeHeader(_, _))
      .WillOnce(Invoke([](Buffer::Instance&,
                          MessageMetadataSharedPtr metadate) -> std::pair<ContextSharedPtr, bool> {
        metadate->setMessageType(MessageType::Response);
        auto context = std::make_shared<ContextImpl>();
        context->set_header_size(16);
        context->set_body_size(10);
        return std::pair<ContextSharedPtr, bool>(context, true);
      }));
  EXPECT_CALL(protocol_, decodeData(_, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, ContextSharedPtr, MessageMetadataSharedPtr) -> bool {
        return false;
      }));

  std::shared_ptr<NiceMock<MockActiveStream>> active_stream;

  EXPECT_CALL(response_callbacks_, newStream()).WillOnce(Invoke([this]() -> StreamHandler& {
    return handler_;
  }));
  EXPECT_CALL(response_callbacks_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(0);

  ResponseDecoder decoder(protocol_, response_callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DubboDecoderTest, DecodeResponseMessage) {
  Buffer::OwnedImpl buffer;
  buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));

  EXPECT_CALL(protocol_, decodeHeader(_, _))
      .WillOnce(Invoke([](Buffer::Instance&,
                          MessageMetadataSharedPtr metadate) -> std::pair<ContextSharedPtr, bool> {
        metadate->setMessageType(MessageType::Response);
        auto context = std::make_shared<ContextImpl>();
        context->set_header_size(16);
        context->set_body_size(10);
        return std::pair<ContextSharedPtr, bool>(context, true);
      }));
  EXPECT_CALL(protocol_, decodeData(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(response_callbacks_, newStream()).WillOnce(ReturnRef(handler_));
  EXPECT_CALL(response_callbacks_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(1);

  ResponseDecoder decoder(protocol_, response_callbacks_);

  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);

  decoder.reset();

  EXPECT_EQ(ProtocolType::Dubbo, decoder.protocol().type());
  EXPECT_CALL(protocol_, decodeHeader(_, _))
      .WillOnce(Invoke([](Buffer::Instance&,
                          MessageMetadataSharedPtr metadate) -> std::pair<ContextSharedPtr, bool> {
        metadate->setMessageType(MessageType::Response);
        auto context = std::make_shared<ContextImpl>();
        context->set_header_size(16);
        context->set_body_size(10);
        return std::pair<ContextSharedPtr, bool>(context, true);
      }));
  EXPECT_CALL(protocol_, decodeData(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(response_callbacks_, newStream()).WillOnce(ReturnRef(handler_));
  EXPECT_CALL(response_callbacks_, onHeartbeat(_)).Times(0);
  EXPECT_CALL(handler_, onStreamDecoded(_, _)).Times(1);

  buffer_underflow = false;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
