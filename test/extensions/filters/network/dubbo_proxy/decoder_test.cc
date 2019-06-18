#include "extensions/filters/network/dubbo_proxy/decoder.h"
#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class DecoderStateMachineTestBase {
public:
  DecoderStateMachineTestBase() : metadata_(std::make_shared<MessageMetadata>()) {
    context_.header_size_ = 16;
  }
  virtual ~DecoderStateMachineTestBase() = default;

  void initHandler() {
    EXPECT_CALL(decoder_callback_, newDecoderEventHandler())
        .WillOnce(Invoke([this]() -> DecoderEventHandler* { return &handler_; }));
  }

  void initProtocolDecoder(MessageType type, int32_t body_size, bool is_heartbeat = false) {
    EXPECT_CALL(protocol_, decode(_, _, _))
        .WillOnce(Invoke([=](Buffer::Instance&, Protocol::Context* context,
                             MessageMetadataSharedPtr metadata) -> bool {
          context->is_heartbeat_ = is_heartbeat;
          context->body_size_ = body_size;
          metadata->setMessageType(type);
          return true;
        }));
  }

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockDeserializer> deserializer_;
  NiceMock<MockDecoderEventHandler> handler_;
  NiceMock<MockDecoderCallbacks> decoder_callback_;
  MessageMetadataSharedPtr metadata_;
  Protocol::Context context_;
};

class DubboDecoderStateMachineTest : public DecoderStateMachineTestBase, public testing::Test {};

class DubboDecoderTest : public testing::Test {
public:
  DubboDecoderTest() = default;
  virtual ~DubboDecoderTest() override = default;

  NiceMock<MockProtocol> protocol_;
  NiceMock<MockDeserializer> deserializer_;
  NiceMock<MockDecoderCallbacks> callbacks_;
};

TEST_F(DubboDecoderStateMachineTest, EmptyData) {
  EXPECT_CALL(protocol_, decode(_, _, _)).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(0);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(0);

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DubboDecoderStateMachineTest, OnlyHaveHeaderData) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 1, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(0);

  Buffer::OwnedImpl buffer;
  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
}

TEST_F(DubboDecoderStateMachineTest, RequestMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 0, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(handler_, transportEnd()).Times(1);

  EXPECT_CALL(deserializer_, deserializeRpcInvocation(_, _, _)).WillOnce(Return());

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
}

TEST_F(DubboDecoderStateMachineTest, ResponseMessageCallbacks) {
  initHandler();
  initProtocolDecoder(MessageType::Response, 0, false);

  EXPECT_CALL(handler_, transportBegin()).Times(1);
  EXPECT_CALL(handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(handler_, transportEnd()).Times(1);

  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        return std::make_unique<RpcResultImpl>(false);
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
}

TEST_F(DubboDecoderStateMachineTest, DeserializeRpcInvocationException) {
  initHandler();
  initProtocolDecoder(MessageType::Request, 0, false);

  EXPECT_CALL(handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(handler_, transportEnd()).Times(0);

  EXPECT_CALL(deserializer_, deserializeRpcInvocation(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, int32_t, MessageMetadataSharedPtr) -> void {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnMessageEnd);
}

TEST_F(DubboDecoderStateMachineTest, DeserializeRpcResultException) {
  initHandler();
  initProtocolDecoder(MessageType::Response, 0, false);

  EXPECT_CALL(handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(handler_, transportEnd()).Times(0);

  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnMessageEnd);
}

TEST_F(DubboDecoderStateMachineTest, ProtocolDecodeException) {
  EXPECT_CALL(decoder_callback_, newDecoderEventHandler()).Times(0);
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context*, MessageMetadataSharedPtr) -> bool {
        throw EnvoyException(fmt::format("mock deserialize exception"));
      }));

  DecoderStateMachine dsm(protocol_, deserializer_, metadata_, decoder_callback_);

  Buffer::OwnedImpl buffer;
  EXPECT_THROW_WITH_MESSAGE(dsm.run(buffer), EnvoyException, "mock deserialize exception");
  EXPECT_EQ(dsm.currentState(), ProtocolState::OnTransportBegin);
}

TEST_F(DubboDecoderTest, NeedMoreDataForProtocolHeader) {
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context*, MessageMetadataSharedPtr) -> bool {
        return false;
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(0);

  Decoder decoder(protocol_, deserializer_, callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DubboDecoderTest, NeedMoreDataForProtocolBody) {
  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([](Buffer::Instance&, Protocol::Context* context,
                          MessageMetadataSharedPtr metadata) -> bool {
        metadata->setMessageType(MessageType::Request);
        context->body_size_ = 10;
        return true;
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportBegin()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageEnd(_)).Times(0);
  EXPECT_CALL(callbacks_.handler_, transferBodyTo(_, _)).Times(0);
  EXPECT_CALL(callbacks_.handler_, transportEnd()).Times(0);

  Decoder decoder(protocol_, deserializer_, callbacks_);

  Buffer::OwnedImpl buffer;
  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, true);
}

TEST_F(DubboDecoderTest, decodeResponseMessage) {
  Buffer::OwnedImpl buffer;
  buffer.add(std::string({'\xda', '\xbb', '\xc2', 0x00}));

  EXPECT_CALL(protocol_, decode(_, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, Protocol::Context* context,
                           MessageMetadataSharedPtr metadata) -> bool {
        metadata->setMessageType(MessageType::Response);
        context->body_size_ = buffer.length();
        return true;
      }));
  EXPECT_CALL(deserializer_, deserializeRpcResult(_, _))
      .WillOnce(Invoke([](Buffer::Instance&, size_t) -> RpcResultPtr {
        return std::make_unique<RpcResultImpl>(true);
      }));
  EXPECT_CALL(callbacks_, newDecoderEventHandler()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportBegin()).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferHeaderTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageBegin(_, _, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, messageEnd(_)).Times(1);
  EXPECT_CALL(callbacks_.handler_, transferBodyTo(_, _)).Times(1);
  EXPECT_CALL(callbacks_.handler_, transportEnd()).Times(1);

  Decoder decoder(protocol_, deserializer_, callbacks_);

  bool buffer_underflow;
  EXPECT_EQ(decoder.onData(buffer, buffer_underflow), Network::FilterStatus::Continue);
  EXPECT_EQ(buffer_underflow, false);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
