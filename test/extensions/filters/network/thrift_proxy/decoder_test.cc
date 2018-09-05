#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/decoder.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Combine;
using testing::DoAll;
using testing::Expectation;
using testing::ExpectationSet;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::StrictMock;
using testing::Test;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

ExpectationSet expectValue(MockProtocol& proto, MockDecoderEventHandler& handler,
                           FieldType field_type, bool result = true) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Bool:
    s += EXPECT_CALL(proto, readBool(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, boolValue(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::Byte:
    s += EXPECT_CALL(proto, readByte(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, byteValue(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::Double:
    s += EXPECT_CALL(proto, readDouble(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, doubleValue(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::I16:
    s += EXPECT_CALL(proto, readInt16(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, int16Value(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::I32:
    s += EXPECT_CALL(proto, readInt32(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, int32Value(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::I64:
    s += EXPECT_CALL(proto, readInt64(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, int64Value(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  case FieldType::String:
    s += EXPECT_CALL(proto, readString(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(handler, stringValue(_)).WillOnce(Return(FilterStatus::Continue));
    }
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

ExpectationSet expectContainerStart(MockProtocol& proto, MockDecoderEventHandler& handler,
                                    FieldType field_type, FieldType inner_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readStructBegin(_, _)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, structBegin(absl::string_view()))
             .WillOnce(Return(FilterStatus::Continue));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(inner_type), SetArgReferee<3>(1), Return(true)));
    s += EXPECT_CALL(handler, fieldBegin(absl::string_view(), inner_type, 1))
             .WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    s += EXPECT_CALL(handler, listBegin(inner_type, 1)).WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(inner_type),
                             SetArgReferee<3>(1), Return(true)));
    s += EXPECT_CALL(handler, mapBegin(inner_type, inner_type, 1))
             .WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    s += EXPECT_CALL(handler, setBegin(inner_type, 1)).WillOnce(Return(FilterStatus::Continue));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

ExpectationSet expectContainerEnd(MockProtocol& proto, MockDecoderEventHandler& handler,
                                  FieldType field_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readFieldEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, fieldEnd()).WillOnce(Return(FilterStatus::Continue));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
    s += EXPECT_CALL(proto, readStructEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, structEnd()).WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, listEnd()).WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, mapEnd()).WillOnce(Return(FilterStatus::Continue));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(handler, setEnd()).WillOnce(Return(FilterStatus::Continue));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

} // end namespace

class DecoderStateMachineTestBase {
public:
  DecoderStateMachineTestBase() : metadata_(std::make_shared<MessageMetadata>()) {}
  virtual ~DecoderStateMachineTestBase() {}

  NiceMock<MockProtocol> proto_;
  MessageMetadataSharedPtr metadata_;
  NiceMock<MockDecoderEventHandler> handler_;
};

class DecoderStateMachineNonValueTest : public DecoderStateMachineTestBase,
                                        public TestWithParam<ProtocolState> {};

static std::string protoStateParamToString(const TestParamInfo<ProtocolState>& params) {
  return ProtocolStateNameValues::name(params.param);
}

INSTANTIATE_TEST_CASE_P(NonValueProtocolStates, DecoderStateMachineNonValueTest,
                        Values(ProtocolState::MessageBegin, ProtocolState::MessageEnd,
                               ProtocolState::StructBegin, ProtocolState::StructEnd,
                               ProtocolState::FieldBegin, ProtocolState::FieldEnd,
                               ProtocolState::MapBegin, ProtocolState::MapEnd,
                               ProtocolState::ListBegin, ProtocolState::ListEnd,
                               ProtocolState::SetBegin, ProtocolState::SetEnd),
                        protoStateParamToString);

class DecoderStateMachineTest : public DecoderStateMachineTestBase, public Test {};

class DecoderStateMachineValueTest : public DecoderStateMachineTestBase,
                                     public TestWithParam<FieldType> {};

INSTANTIATE_TEST_CASE_P(PrimitiveFieldTypes, DecoderStateMachineValueTest,
                        Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                               FieldType::I32, FieldType::I64, FieldType::String),
                        fieldTypeParamToString);

class DecoderStateMachineNestingTest
    : public DecoderStateMachineTestBase,
      public TestWithParam<std::tuple<FieldType, FieldType, FieldType>> {};

static std::string nestedFieldTypesParamToString(
    const TestParamInfo<std::tuple<FieldType, FieldType, FieldType>>& params) {
  FieldType outer_field_type, inner_type, value_type;
  std::tie(outer_field_type, inner_type, value_type) = params.param;
  return fmt::format("{}Of{}Of{}", fieldTypeToString(outer_field_type),
                     fieldTypeToString(inner_type), fieldTypeToString(value_type));
}

INSTANTIATE_TEST_CASE_P(
    NestedTypes, DecoderStateMachineNestingTest,
    Combine(Values(FieldType::Struct, FieldType::List, FieldType::Map, FieldType::Set),
            Values(FieldType::Struct, FieldType::List, FieldType::Map, FieldType::Set),
            Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                   FieldType::I32, FieldType::I64, FieldType::String)),
    nestedFieldTypesParamToString);

TEST_P(DecoderStateMachineNonValueTest, NoData) {
  ProtocolState state = GetParam();
  Buffer::OwnedImpl buffer;

  DecoderStateMachine dsm(proto_, metadata_, handler_);
  dsm.setCurrentState(state);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), state);
}

TEST_P(DecoderStateMachineValueTest, NoFieldValueData) {
  FieldType field_type = GetParam();

  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));
  expectValue(proto_, handler_, field_type, false);
  expectValue(proto_, handler_, field_type, true);
  EXPECT_CALL(proto_, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldValue);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldBegin);
}

TEST_P(DecoderStateMachineValueTest, FieldValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto_, handler_, field_type);

  EXPECT_CALL(proto_, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldBegin);
}

TEST_F(DecoderStateMachineTest, NoListValueData) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto_, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListValue);
}

TEST_F(DecoderStateMachineTest, EmptyList) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto_, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, ListValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto_, handler_, field_type);

  EXPECT_CALL(proto_, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleListValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto_, handler_, field_type);
  }

  EXPECT_CALL(proto_, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_F(DecoderStateMachineTest, NoMapKeyData) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto_, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapKey);
}

TEST_F(DecoderStateMachineTest, NoMapValueData) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto_, readInt32(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto_, readString(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapValue);
}

TEST_F(DecoderStateMachineTest, EmptyMap) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(0), Return(true)));
  EXPECT_CALL(proto_, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapKeyValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto_, handler_, field_type);        // key
  expectValue(proto_, handler_, FieldType::String); // value

  EXPECT_CALL(proto_, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapValueValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto_, handler_, FieldType::I32); // key
  expectValue(proto_, handler_, field_type);     // value

  EXPECT_CALL(proto_, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleMapKeyValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto_, handler_, FieldType::I32); // key
    expectValue(proto_, handler_, field_type);     // value
  }

  EXPECT_CALL(proto_, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_F(DecoderStateMachineTest, NoSetValueData) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto_, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetValue);
}

TEST_F(DecoderStateMachineTest, EmptySet) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto_, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, SetValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto_, handler_, field_type);

  EXPECT_CALL(proto_, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleSetValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto_, handler_, field_type);
  }

  EXPECT_CALL(proto_, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_F(DecoderStateMachineTest, EmptyStruct) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(proto_, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto_, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto_, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineValueTest, SingleFieldStruct) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  EXPECT_CALL(proto_, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(proto_, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(handler_, structBegin(absl::string_view())).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(handler_, fieldBegin(absl::string_view(), field_type, 1))
      .WillOnce(Return(FilterStatus::Continue));

  expectValue(proto_, handler_, field_type);

  EXPECT_CALL(proto_, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, fieldEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));

  EXPECT_CALL(proto_, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, structEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(proto_, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_F(DecoderStateMachineTest, MultiFieldStruct) {
  Buffer::OwnedImpl buffer;
  InSequence dummy;

  std::vector<FieldType> field_types = {FieldType::Bool,  FieldType::Byte, FieldType::Double,
                                        FieldType::I16,   FieldType::I32,  FieldType::I64,
                                        FieldType::String};

  EXPECT_CALL(proto_, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(proto_, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(handler_, structBegin(absl::string_view())).WillOnce(Return(FilterStatus::Continue));

  int16_t field_id = 1;
  for (FieldType field_type : field_types) {
    EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(field_id), Return(true)));
    EXPECT_CALL(handler_, fieldBegin(absl::string_view(), field_type, field_id))
        .WillOnce(Return(FilterStatus::Continue));
    field_id++;

    expectValue(proto_, handler_, field_type);

    EXPECT_CALL(proto_, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
    EXPECT_CALL(handler_, fieldEnd()).WillOnce(Return(FilterStatus::Continue));
  }

  EXPECT_CALL(proto_, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto_, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, structEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(proto_, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineNestingTest, NestedTypes) {
  FieldType outer_field_type, inner_type, value_type;
  std::tie(outer_field_type, inner_type, value_type) = GetParam();

  Buffer::OwnedImpl buffer;
  InSequence dummy;

  // start of message and outermost struct
  EXPECT_CALL(proto_, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::Continue;
      }));

  expectContainerStart(proto_, handler_, FieldType::Struct, outer_field_type);

  expectContainerStart(proto_, handler_, outer_field_type, inner_type);

  int outer_reps = outer_field_type == FieldType::Map ? 2 : 1;
  for (int i = 0; i < outer_reps; i++) {
    expectContainerStart(proto_, handler_, inner_type, value_type);

    int inner_reps = inner_type == FieldType::Map ? 2 : 1;
    for (int j = 0; j < inner_reps; j++) {
      expectValue(proto_, handler_, value_type);
    }

    expectContainerEnd(proto_, handler_, inner_type);
  }

  expectContainerEnd(proto_, handler_, outer_field_type);

  // end of message and outermost struct
  expectContainerEnd(proto_, handler_, FieldType::Struct);

  EXPECT_CALL(proto_, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler_, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  DecoderStateMachine dsm(proto_, metadata_, handler_);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST(DecoderTest, OnData) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        return true;
      }));
  EXPECT_CALL(handler, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasFrameSize());
        EXPECT_EQ(100U, metadata->frameSize());
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(handler, structBegin(absl::string_view())).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, structEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, transportEnd()).WillOnce(Return(FilterStatus::Continue));

  bool underflow = false;
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);
}

TEST(DecoderTest, OnDataWithProtocolHint) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        metadata.setProtocol(ProtocolType::Binary);
        return true;
      }));
  EXPECT_CALL(*proto, type()).WillOnce(Return(ProtocolType::Auto));
  EXPECT_CALL(*proto, setType(ProtocolType::Binary));
  EXPECT_CALL(handler, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasFrameSize());
        EXPECT_EQ(100U, metadata->frameSize());

        EXPECT_TRUE(metadata->hasProtocol());
        EXPECT_EQ(ProtocolType::Binary, metadata->protocol());

        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(handler, structBegin(absl::string_view())).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, structEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, transportEnd()).WillOnce(Return(FilterStatus::Continue));

  bool underflow = false;
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);
}

TEST(DecoderTest, OnDataWithInconsistentProtocolHint) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        metadata.setProtocol(ProtocolType::Binary);
        return true;
      }));
  EXPECT_CALL(*proto, type()).WillRepeatedly(Return(ProtocolType::Compact));

  bool underflow = false;
  EXPECT_THROW_WITH_MESSAGE(decoder.onData(buffer, underflow), EnvoyException,
                            "transport reports protocol binary, but configured for compact");
}

TEST(DecoderTest, OnDataThrowsTransportAppException) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setAppException(AppExceptionType::InvalidTransform, "unknown xform");
        return true;
      }));

  bool underflow = false;
  EXPECT_THROW_WITH_MESSAGE(decoder.onData(buffer, underflow), AppException, "unknown xform");
}

TEST(DecoderTest, OnDataResumes) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  buffer.add("x");

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        return true;
      }));
  EXPECT_CALL(*proto, readMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(false));

  bool underflow = false;
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));

  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow); // buffer.length() == 1
}

TEST(DecoderTest, OnDataResumesTransportFrameStart) {
  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  EXPECT_CALL(*transport, name()).Times(AnyNumber());
  EXPECT_CALL(*proto, name()).Times(AnyNumber());

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  bool underflow = false;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _)).WillOnce(Return(false));
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        return true;
      }));
  EXPECT_CALL(*proto, readMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));

  underflow = false;
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow); // buffer.length() == 0
}

TEST(DecoderTest, OnDataResumesTransportFrameEnd) {
  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  EXPECT_CALL(*transport, name()).Times(AnyNumber());
  EXPECT_CALL(*proto, name()).Times(AnyNumber());

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        return true;
      }));
  EXPECT_CALL(*proto, readMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(false));

  bool underflow = false;
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));
  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow); // buffer.length() == 0
}

TEST(DecoderTest, OnDataHandlesStopIterationAndResumes) {

  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  EXPECT_CALL(*transport, name()).WillRepeatedly(ReturnRef(transport->name_));

  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  EXPECT_CALL(*proto, name()).WillRepeatedly(ReturnRef(proto->name_));

  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<MockDecoderEventHandler> handler;
  ON_CALL(callbacks, newDecoderEventHandler()).WillByDefault(ReturnRef(handler));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  bool underflow = true;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setFrameSize(100);
        return true;
      }));
  EXPECT_CALL(handler, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasFrameSize());
        EXPECT_EQ(100U, metadata->frameSize());

        return FilterStatus::StopIteration;
      }));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setMethodName("name");
        metadata.setMessageType(MessageType::Call);
        metadata.setSequenceId(100);
        return true;
      }));
  EXPECT_CALL(handler, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_TRUE(metadata->hasMethodName());
        EXPECT_TRUE(metadata->hasMessageType());
        EXPECT_TRUE(metadata->hasSequenceId());
        EXPECT_EQ("name", metadata->methodName());
        EXPECT_EQ(MessageType::Call, metadata->messageType());
        EXPECT_EQ(100U, metadata->sequenceId());
        return FilterStatus::StopIteration;
      }));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(handler, structBegin(absl::string_view()))
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::I32), SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(handler, fieldBegin(absl::string_view(), FieldType::I32, 1))
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readInt32(_, _)).WillOnce(Return(true));
  EXPECT_CALL(handler, int32Value(_)).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, fieldEnd()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, structEnd()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, messageEnd()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(handler, transportEnd()).WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_EQ(FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_EQ(FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);
}

#define TEST_NAME(X) EXPECT_EQ(ProtocolStateNameValues::name(ProtocolState::X), #X);

TEST(ProtocolStateNameValuesTest, ValidNames) { ALL_PROTOCOL_STATES(TEST_NAME) }

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
