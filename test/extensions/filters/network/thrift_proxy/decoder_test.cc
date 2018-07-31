#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/decoder.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
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
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

ExpectationSet expectValue(MockProtocol& proto, ThriftFilters::MockDecoderFilter& filter,
                           FieldType field_type, bool result = true) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Bool:
    s += EXPECT_CALL(proto, readBool(_, _)).WillOnce(Return(result));
    if (result) {
      s +=
          EXPECT_CALL(filter, boolValue(_)).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::Byte:
    s += EXPECT_CALL(proto, readByte(_, _)).WillOnce(Return(result));
    if (result) {
      s +=
          EXPECT_CALL(filter, byteValue(_)).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::Double:
    s += EXPECT_CALL(proto, readDouble(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(filter, doubleValue(_))
               .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::I16:
    s += EXPECT_CALL(proto, readInt16(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(filter, int16Value(_))
               .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::I32:
    s += EXPECT_CALL(proto, readInt32(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(filter, int32Value(_))
               .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::I64:
    s += EXPECT_CALL(proto, readInt64(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(filter, int64Value(_))
               .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  case FieldType::String:
    s += EXPECT_CALL(proto, readString(_, _)).WillOnce(Return(result));
    if (result) {
      s += EXPECT_CALL(filter, stringValue(_))
               .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    }
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

ExpectationSet expectContainerStart(MockProtocol& proto, ThriftFilters::MockDecoderFilter& filter,
                                    FieldType field_type, FieldType inner_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readStructBegin(_, _)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, structBegin(absl::string_view()))
             .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(inner_type), SetArgReferee<3>(1), Return(true)));
    s += EXPECT_CALL(filter, fieldBegin(absl::string_view(), inner_type, 1))
             .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    s += EXPECT_CALL(filter, listBegin(inner_type, 1))
             .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(inner_type),
                             SetArgReferee<3>(1), Return(true)));
    s += EXPECT_CALL(filter, mapBegin(inner_type, inner_type, 1))
             .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetBegin(_, _, _))
             .WillOnce(DoAll(SetArgReferee<1>(inner_type), SetArgReferee<2>(1), Return(true)));
    s += EXPECT_CALL(filter, setBegin(inner_type, 1))
             .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

ExpectationSet expectContainerEnd(MockProtocol& proto, ThriftFilters::MockDecoderFilter& filter,
                                  FieldType field_type) {
  ExpectationSet s;
  switch (field_type) {
  case FieldType::Struct:
    s += EXPECT_CALL(proto, readFieldEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, fieldEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    s += EXPECT_CALL(proto, readFieldBegin(_, _, _, _))
             .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
    s += EXPECT_CALL(proto, readStructEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, structEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::List:
    s += EXPECT_CALL(proto, readListEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, listEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::Map:
    s += EXPECT_CALL(proto, readMapEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, mapEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  case FieldType::Set:
    s += EXPECT_CALL(proto, readSetEnd(_)).WillOnce(Return(true));
    s += EXPECT_CALL(filter, setEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return s;
}

} // end namespace

class DecoderStateMachineNonValueTest : public TestWithParam<ProtocolState> {};

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

class DecoderStateMachineValueTest : public TestWithParam<FieldType> {};

INSTANTIATE_TEST_CASE_P(PrimitiveFieldTypes, DecoderStateMachineValueTest,
                        Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                               FieldType::I32, FieldType::I64, FieldType::String),
                        fieldTypeParamToString);

class DecoderStateMachineNestingTest
    : public TestWithParam<std::tuple<FieldType, FieldType, FieldType>> {};

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
  NiceMock<MockProtocol> proto;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  DecoderStateMachine dsm(proto, filter);
  dsm.setCurrentState(state);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), state);
}

TEST_P(DecoderStateMachineValueTest, NoFieldValueData) {
  FieldType field_type = GetParam();

  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));
  expectValue(proto, filter, field_type, false);
  expectValue(proto, filter, field_type, true);
  EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldValue);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldBegin);
}

TEST_P(DecoderStateMachineValueTest, FieldValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(std::string("")), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, filter, field_type);

  EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::FieldBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::FieldBegin);
}

TEST(DecoderStateMachineTest, NoListValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListValue);
}

TEST(DecoderStateMachineTest, EmptyList) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, ListValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto, filter, field_type);

  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleListValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readListBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, filter, field_type);
  }

  EXPECT_CALL(proto, readListEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::ListBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::ListEnd);
}

TEST(DecoderStateMachineTest, NoMapKeyData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapKey);
}

TEST(DecoderStateMachineTest, NoMapValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto, readString(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapValue);
}

TEST(DecoderStateMachineTest, EmptyMap) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(0), Return(true)));
  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapKeyValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(FieldType::String),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, filter, field_type);        // key
  expectValue(proto, filter, FieldType::String); // value

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MapValueValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(1), Return(true)));

  expectValue(proto, filter, FieldType::I32); // key
  expectValue(proto, filter, field_type);     // value

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleMapKeyValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMapBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(field_type),
                      SetArgReferee<3>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, filter, FieldType::I32); // key
    expectValue(proto, filter, field_type);     // value
  }

  EXPECT_CALL(proto, readMapEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::MapBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::MapEnd);
}

TEST(DecoderStateMachineTest, NoSetValueData) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(1), Return(true)));
  EXPECT_CALL(proto, readInt32(Ref(buffer), _)).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetValue);
}

TEST(DecoderStateMachineTest, EmptySet) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(FieldType::I32), SetArgReferee<2>(0), Return(true)));
  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, SetValue) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(1), Return(true)));

  expectValue(proto, filter, field_type);

  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST_P(DecoderStateMachineValueTest, MultipleSetValues) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readSetBegin(Ref(buffer), _, _))
      .WillOnce(DoAll(SetArgReferee<1>(field_type), SetArgReferee<2>(5), Return(true)));

  for (int i = 0; i < 5; i++) {
    expectValue(proto, filter, field_type);
  }

  EXPECT_CALL(proto, readSetEnd(Ref(buffer))).WillOnce(Return(false));

  DecoderStateMachine dsm(proto, filter);

  dsm.setCurrentState(ProtocolState::SetBegin);
  EXPECT_EQ(dsm.run(buffer), ProtocolState::WaitForData);
  EXPECT_EQ(dsm.currentState(), ProtocolState::SetEnd);
}

TEST(DecoderStateMachineTest, EmptyStruct) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));

  DecoderStateMachine dsm(proto, filter);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineValueTest, SingleFieldStruct) {
  FieldType field_type = GetParam();
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(filter, messageBegin(absl::string_view("name"), MessageType::Call, 100))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(filter, structBegin(absl::string_view()))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(filter, fieldBegin(absl::string_view(), field_type, 1))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  expectValue(proto, filter, field_type);

  EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, fieldEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));

  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, structEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, messageEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  DecoderStateMachine dsm(proto, filter);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST(DecoderStateMachineTest, MultiFieldStruct) {
  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  std::vector<FieldType> field_types = {FieldType::Bool,  FieldType::Byte, FieldType::Double,
                                        FieldType::I16,   FieldType::I32,  FieldType::I64,
                                        FieldType::String};

  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(filter, messageBegin(absl::string_view("name"), MessageType::Call, 100))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(filter, structBegin(absl::string_view()))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  int16_t field_id = 1;
  for (FieldType field_type : field_types) {
    EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(field_type), SetArgReferee<3>(field_id), Return(true)));
    EXPECT_CALL(filter, fieldBegin(absl::string_view(), field_type, field_id))
        .WillOnce(Return(ThriftFilters::FilterStatus::Continue));
    field_id++;

    expectValue(proto, filter, field_type);

    EXPECT_CALL(proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
    EXPECT_CALL(filter, fieldEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));
  }

  EXPECT_CALL(proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, structEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, messageEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  DecoderStateMachine dsm(proto, filter);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST_P(DecoderStateMachineNestingTest, NestedTypes) {
  FieldType outer_field_type, inner_type, value_type;
  std::tie(outer_field_type, inner_type, value_type) = GetParam();

  Buffer::OwnedImpl buffer;
  NiceMock<MockProtocol> proto;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  InSequence dummy;

  // start of message and outermost struct
  EXPECT_CALL(proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(filter, messageBegin(absl::string_view("name"), MessageType::Call, 100))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  expectContainerStart(proto, filter, FieldType::Struct, outer_field_type);

  expectContainerStart(proto, filter, outer_field_type, inner_type);

  int outer_reps = outer_field_type == FieldType::Map ? 2 : 1;
  for (int i = 0; i < outer_reps; i++) {
    expectContainerStart(proto, filter, inner_type, value_type);

    int inner_reps = inner_type == FieldType::Map ? 2 : 1;
    for (int j = 0; j < inner_reps; j++) {
      expectValue(proto, filter, value_type);
    }

    expectContainerEnd(proto, filter, inner_type);
  }

  expectContainerEnd(proto, filter, outer_field_type);

  // end of message and outermost struct
  expectContainerEnd(proto, filter, FieldType::Struct);

  EXPECT_CALL(proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, messageEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  DecoderStateMachine dsm(proto, filter);

  EXPECT_EQ(dsm.run(buffer), ProtocolState::Done);
  EXPECT_EQ(dsm.currentState(), ProtocolState::Done);
}

TEST(DecoderTest, OnData) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  ON_CALL(callbacks, newDecoderFilter()).WillByDefault(ReturnRef(filter));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(true)));
  EXPECT_CALL(filter, transportBegin(absl::optional<uint32_t>(100)))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(filter, messageBegin(absl::string_view("name"), MessageType::Call, 100))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(filter, structBegin(absl::string_view()))
      .WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, structEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, messageEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, transportEnd()).WillOnce(Return(ThriftFilters::FilterStatus::Continue));

  bool underflow = false;
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);
}

TEST(DecoderTest, OnDataResumes) {
  NiceMock<MockTransport>* transport = new NiceMock<MockTransport>();
  NiceMock<MockProtocol>* proto = new NiceMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  ON_CALL(callbacks, newDecoderFilter()).WillByDefault(ReturnRef(filter));

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  buffer.add("x");

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(true)));
  EXPECT_CALL(*proto, readMessageBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(false));

  bool underflow = false;
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));

  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow); // buffer.length() == 1
}

TEST(DecoderTest, OnDataResumesTransportFrameStart) {
  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  ON_CALL(callbacks, newDecoderFilter()).WillByDefault(ReturnRef(filter));

  EXPECT_CALL(*transport, name()).Times(AnyNumber());
  EXPECT_CALL(*proto, name()).Times(AnyNumber());

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  bool underflow = false;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(false)));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(true)));
  EXPECT_CALL(*proto, readMessageBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));

  underflow = false;
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow); // buffer.length() == 0
}

TEST(DecoderTest, OnDataResumesTransportFrameEnd) {
  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  NiceMock<MockDecoderCallbacks> callbacks;
  NiceMock<ThriftFilters::MockDecoderFilter> filter;
  ON_CALL(callbacks, newDecoderFilter()).WillByDefault(ReturnRef(filter));

  EXPECT_CALL(*transport, name()).Times(AnyNumber());
  EXPECT_CALL(*proto, name()).Times(AnyNumber());

  InSequence dummy;

  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(true)));
  EXPECT_CALL(*proto, readMessageBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(*proto, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readFieldBegin(_, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*proto, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(false));

  bool underflow = false;
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);

  EXPECT_CALL(*transport, decodeFrameEnd(_)).WillOnce(Return(true));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow); // buffer.length() == 0
}

TEST(DecoderTest, OnDataHandlesStopIterationAndResumes) {

  StrictMock<MockTransport>* transport = new StrictMock<MockTransport>();
  EXPECT_CALL(*transport, name()).WillRepeatedly(ReturnRef(transport->name_));

  StrictMock<MockProtocol>* proto = new StrictMock<MockProtocol>();
  EXPECT_CALL(*proto, name()).WillRepeatedly(ReturnRef(proto->name_));

  NiceMock<MockDecoderCallbacks> callbacks;
  StrictMock<ThriftFilters::MockDecoderFilter> filter;
  ON_CALL(callbacks, newDecoderFilter()).WillByDefault(ReturnRef(filter));

  InSequence dummy;
  Decoder decoder(TransportPtr{transport}, ProtocolPtr{proto}, callbacks);
  Buffer::OwnedImpl buffer;
  bool underflow = true;

  EXPECT_CALL(*transport, decodeFrameStart(Ref(buffer), _))
      .WillOnce(DoAll(SetArgReferee<1>(absl::optional<uint32_t>(100)), Return(true)));
  EXPECT_CALL(filter, transportBegin(absl::optional<uint32_t>(100)))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readMessageBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<1>("name"), SetArgReferee<2>(MessageType::Call),
                      SetArgReferee<3>(100), Return(true)));
  EXPECT_CALL(filter, messageBegin(absl::string_view("name"), MessageType::Call, 100))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readStructBegin(Ref(buffer), _)).WillOnce(Return(true));
  EXPECT_CALL(filter, structBegin(absl::string_view()))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::I32), SetArgReferee<3>(1), Return(true)));
  EXPECT_CALL(filter, fieldBegin(absl::string_view(), FieldType::I32, 1))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readInt32(_, _)).WillOnce(Return(true));
  EXPECT_CALL(filter, int32Value(_)).WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, fieldEnd()).WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readFieldBegin(Ref(buffer), _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(FieldType::Stop), Return(true)));
  EXPECT_CALL(*proto, readStructEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, structEnd()).WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*proto, readMessageEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, messageEnd()).WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_CALL(*transport, decodeFrameEnd(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(filter, transportEnd()).WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration, decoder.onData(buffer, underflow));
  EXPECT_FALSE(underflow);

  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, decoder.onData(buffer, underflow));
  EXPECT_TRUE(underflow);
}

#define TEST_NAME(X) EXPECT_EQ(ProtocolStateNameValues::name(ProtocolState::X), #X);

TEST(ProtocolStateNameValuesTest, ValidNames) { ALL_PROTOCOL_STATES(TEST_NAME) }

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
