#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/decoder.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::Combine;
using testing::DoAll;
using testing::Expectation;
using testing::ExpectationSet;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::StrictMock;
using testing::TestWithParam;
using testing::Values;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

Expectation expectValue(NiceMock<MockProtocol>& proto, FieldType field_type, bool result = true) {
  switch (field_type) {
  case FieldType::Bool:
    return EXPECT_CALL(proto, readBool(_, _)).WillOnce(Return(result));
  case FieldType::Byte:
    return EXPECT_CALL(proto, readByte(_, _)).WillOnce(Return(result));
  case FieldType::Double:
    return EXPECT_CALL(proto, readDouble(_, _)).WillOnce(Return(result));
  case FieldType::I16:
    return EXPECT_CALL(proto, readInt16(_, _)).WillOnce(Return(result));
  case FieldType::I32:
    return EXPECT_CALL(proto, readInt32(_, _)).WillOnce(Return(result));
  case FieldType::I64:
    return EXPECT_CALL(proto, readInt64(_, _)).WillOnce(Return(result));
  case FieldType::String:
    return EXPECT_CALL(proto, readString(_, _)).WillOnce(Return(result));
  default:
    NOT_REACHED;
  }
}

ExpectationSet expectContainerStart(NiceMock<MockProtocol>& proto, FieldType field_type,
                                    FieldType inner_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readStructBegin(_, _)).WillOnce(Return(true));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(inner_type), SetArgReferee<3>(1), Return(true)));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(inner_type),
                             SetArgReferee<3>(1), Return(true)));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    break;
  default:
    NOT_REACHED;
  }
  return s;
}

ExpectationSet expectContainerEnd(NiceMock<MockProtocol>& proto, FieldType field_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readFieldEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
    s += EXPECT_CALL(proto, readStructEnd(_)).WillOnce(Return(true));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListEnd(_)).WillOnce(Return(true));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapEnd(_)).WillOnce(Return(true));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetEnd(_)).WillOnce(Return(true));
    break;
  default:
    NOT_REACHED;
  }
  return s;
}

} // end namespace

class DecoderStateMachineNonValueTest : public TestWithParam<ProtocolState> {};

INSTANTIATE_TEST_CASE_P(NonValueProtocolStates, DecoderStateMachineNonValueTest,
                        Values(ProtocolState::MessageBegin, ProtocolState::MessageEnd,
                               ProtocolState::StructBegin, ProtocolState::StructEnd,
                               ProtocolState::FieldBegin, ProtocolState::FieldEnd,
                               ProtocolState::MapBegin, ProtocolState::MapEnd,
                               ProtocolState::ListBegin, ProtocolState::ListEnd,
                               ProtocolState::SetBegin, ProtocolState::SetEnd));

class DecoderStateMachineValueTest : public TestWithParam<FieldType> {};

INSTANTIATE_TEST_CASE_P(PrimitiveFieldTypes, DecoderStateMachineValueTest,
                        Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                               FieldType::I32, FieldType::I64, FieldType::String));

class DecoderStateMachineNestingTest
    : public TestWithParam<std::tuple<FieldType, FieldType, FieldType>> {};

INSTANTIATE_TEST_CASE_P(
    NestedTypes, DecoderStateMachineNestingTest,
    Combine(Values(FieldType::Struct, FieldType::List, FieldType::Map, FieldType::Set),
            Values(FieldType::Struct, FieldType::List, FieldType::Map, FieldType::Set),
            Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                   FieldType::I32, FieldType::I64, FieldType::String)));

TEST_P(DecoderStateMachineNonValueTest, NoData) {
  ProtocolState state = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  DecoderStateMachine dsm(proto);
  dsm.setCurrentState(state);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), state);
}

TEST_P(DecoderStateMachineValueTest, NoFieldValueData) {
  FieldType field_type = GetParam();

  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));
  expectValue(proto, field_type, false);

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldValue);
}

TEST_P(DecoderStateMachineValueTest, FieldValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, field_type);

  EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldBegin);
}

TEST(DecoderStateMachineTest, NoListValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListValue);
}

TEST(DecoderStateMachineTest, EmptyList) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, ListValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto, field_type);

  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleListValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, field_type);
  }

  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST(DecoderStateMachineTest, NoMapKeyData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapKey);
}

TEST(DecoderStateMachineTest, NoMapValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto, readString(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapValue);
}

TEST(DecoderStateMachineTest, EmptyMap) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(0), Return(true)));
  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapKeyValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, field_type);        // key
  expectValue(proto, FieldType::String); // value

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapValueValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, FieldType::I32); // key
  expectValue(proto, field_type);     // value

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleMapKeyValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, FieldType::I32); // key
    expectValue(proto, field_type);     // value
  }

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST(DecoderStateMachineTest, NoSetValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetValue);
}

TEST(DecoderStateMachineTest, EmptySet) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, SetValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto, field_type);

  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleSetValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, field_type);
  }

  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST(DecoderStateMachineTest, EmptyStruct) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineValueTest, SingleFieldStruct) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(1), Return(true)));

  expectValue(proto, field_type);

  EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST(DecoderStateMachineTest, MultiFieldStruct) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  std::vector<FieldType> field_types = {FieldType::Bool,  FieldType::Byte, FieldType::Double,
                                        FieldType::I16,   FieldType::I32,  FieldType::I64,
                                        FieldType::String};

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));

  int16_t field_id = 1;
  for (FieldType field_type : field_types) {
    EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(field_id++), Return(true)));

    expectValue(proto, field_type);

    EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  }

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineNestingTest, NestedTypes) {
  FieldType outer_field_type, inner_type, value_type;
  std::tie(outer_field_type, inner_type, value_type) = GetParam();

  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  InSequence dummy;

  // start of message and outermost struct
  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  expectContainerStart(proto, FieldType::Struct, outer_field_type);

  expectContainerStart(proto, outer_field_type, inner_type);

  int outer_reps = outer_field_type == FieldType::Map ? 2 : 1;
  for (int i = 0; i < outer_reps; i++) {
    expectContainerStart(proto, inner_type, value_type);

    int inner_reps = inner_type == FieldType::Map ? 2 : 1;
    for (int j = 0; j < inner_reps; j++) {
      expectValue(proto, value_type);
    }

    expectContainerEnd(proto, inner_type);
  }

  expectContainerEnd(proto, outer_field_type);

  // end of message and outermost struct
  expectContainerEnd(proto, FieldType::Struct);
  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST(DecoderTest, OnData) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto});
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer))).WillOnce(Return(false));

  decoder.onData(buffer);
}

TEST(DecoderTest, OnDataResumes) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto});
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(false));
  decoder.onData(buffer);

  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameStart(_)).WillOnce(Return(false));
  decoder.onData(buffer);
}

TEST(DecoderTest, OnDataResumesTransportFrameEnd) {
  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();

  EXPECT_CALL(*transport, name()).Times(AnyNumber());
  EXPECT_CALL(*proto, name()).Times(AnyNumber());

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto});
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(false));
  decoder.onData(buffer);

  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameStart(_)).WillOnce(Return(false));
  decoder.onData(buffer);
}

#define TEST_NAME(X) EXPECT_EQ(ProtocolStateNameValues::name(ProtocolState::X), #X);

TEST(ProtocolStateNameValuesTest, ValidNames) { ALL_PROTOCOL_STATES(TEST_NAME) }

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
