#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/thrift_object_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Expectation;
using testing::ExpectationSet;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftObjectImplTestBase {
public:
  virtual ~ThriftObjectImplTestBase() = default;

  Expectation expectValue(FieldType field_type) {
    switch (field_type) {
    case FieldType::Bool:
      return EXPECT_CALL(protocol_, readBool(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, bool& value) -> bool {
            value = true;
            return true;
          }));
    case FieldType::Byte:
      return EXPECT_CALL(protocol_, readByte(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, uint8_t& value) -> bool {
            value = 1;
            return true;
          }));
    case FieldType::Double:
      return EXPECT_CALL(protocol_, readDouble(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, double& value) -> bool {
            value = 2.0;
            return true;
          }));
    case FieldType::I16:
      return EXPECT_CALL(protocol_, readInt16(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, int16_t& value) -> bool {
            value = 3;
            return true;
          }));
    case FieldType::I32:
      return EXPECT_CALL(protocol_, readInt32(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, int32_t& value) -> bool {
            value = 4;
            return true;
          }));
    case FieldType::I64:
      return EXPECT_CALL(protocol_, readInt64(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, int64_t& value) -> bool {
            value = 5;
            return true;
          }));
    case FieldType::String:
      return EXPECT_CALL(protocol_, readString(Ref(buffer_), _))
          .WillOnce(Invoke([](Buffer::Instance&, std::string& value) -> bool {
            value = "six";
            return true;
          }));
    default:
      PANIC("reached unexpected code");
    }
  }

  Expectation expectFieldBegin(FieldType field_type, int16_t field_id) {
    return EXPECT_CALL(protocol_, readFieldBegin(Ref(buffer_), _, _, _))
        .WillOnce(
            Invoke([=](Buffer::Instance&, std::string&, FieldType& type, int16_t& id) -> bool {
              type = field_type;
              id = field_id;
              return true;
            }));
  }

  Expectation expectFieldEnd() {
    return EXPECT_CALL(protocol_, readFieldEnd(Ref(buffer_))).WillOnce(Return(true));
  }

  ExpectationSet expectField(FieldType field_type, int16_t field_id) {
    ExpectationSet s;
    s += expectFieldBegin(field_type, field_id);
    s += expectValue(field_type);
    s += expectFieldEnd();
    return s;
  }

  Expectation expectStopField() { return expectFieldBegin(FieldType::Stop, 0); }

  void checkValue(FieldType field_type, const ThriftValue& value) {
    EXPECT_EQ(field_type, value.type());

    switch (field_type) {
    case FieldType::Bool:
      EXPECT_EQ(true, value.getValueTyped<bool>());
      break;
    case FieldType::Byte:
      EXPECT_EQ(1, value.getValueTyped<uint8_t>());
      break;
    case FieldType::Double:
      EXPECT_EQ(2.0, value.getValueTyped<double>());
      break;
    case FieldType::I16:
      EXPECT_EQ(3, value.getValueTyped<int16_t>());
      break;
    case FieldType::I32:
      EXPECT_EQ(4, value.getValueTyped<int32_t>());
      break;
    case FieldType::I64:
      EXPECT_EQ(5, value.getValueTyped<int64_t>());
      break;
    case FieldType::String:
      EXPECT_EQ("six", value.getValueTyped<std::string>());
      break;
    default:
      PANIC("reached unexpected code");
    }
  }

  void checkFieldValue(const ThriftField& field) {
    const ThriftValue& value = field.getValue();
    checkValue(field.fieldType(), value);
  }

  NiceMock<MockTransport> transport_;
  NiceMock<MockProtocol> protocol_;
  Buffer::OwnedImpl buffer_;
};

class ThriftObjectImplTest : public testing::Test, public ThriftObjectImplTestBase {};

// Test parsing an empty struct (just a stop field).
TEST_F(ThriftObjectImplTest, ParseEmptyStruct) {
  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_TRUE(thrift_obj.fields().empty());
}

class ThriftObjectImplValueTest : public ThriftObjectImplTestBase,
                                  public testing::TestWithParam<FieldType> {};

INSTANTIATE_TEST_SUITE_P(PrimitiveFieldTypes, ThriftObjectImplValueTest,
                         Values(FieldType::Bool, FieldType::Byte, FieldType::Double, FieldType::I16,
                                FieldType::I32, FieldType::I64, FieldType::String),
                         fieldTypeParamToString);

// Test parsing a struct with a single field with a simple value.
TEST_P(ThriftObjectImplValueTest, ParseSingleValueStruct) {
  FieldType field_type = GetParam();

  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectField(field_type, 1);
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());
  EXPECT_EQ(field_type, thrift_obj.fields().front()->fieldType());
  EXPECT_EQ(1, thrift_obj.fields().front()->fieldId());
  checkFieldValue(*thrift_obj.fields().front());
}

// Test parsing nested structs (struct -> struct -> simple field).
TEST_P(ThriftObjectImplValueTest, ParseNestedSingleValueStruct) {
  FieldType field_type = GetParam();

  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectFieldBegin(FieldType::Struct, 1);

  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectField(field_type, 2);
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));

  expectFieldEnd();
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());
  const ThriftField& field = *thrift_obj.fields().front();
  EXPECT_EQ(FieldType::Struct, field.fieldType());

  const ThriftStructValue& nested = field.getValue().getValueTyped<ThriftStructValue>();
  EXPECT_EQ(1, nested.fields().size());
  EXPECT_EQ(field_type, nested.fields().front()->fieldType());
  EXPECT_EQ(2, nested.fields().front()->fieldId());
  checkFieldValue(*nested.fields().front());
}

// Test parsing a struct with a single list field (struct -> list).
TEST_P(ThriftObjectImplValueTest, ParseNestedListValue) {
  FieldType field_type = GetParam();

  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectFieldBegin(FieldType::List, 1);

  EXPECT_CALL(protocol_, readListBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& type, uint32_t& size) -> bool {
        type = field_type;
        size = 2;
        return true;
      }));
  expectValue(field_type);
  expectValue(field_type);
  EXPECT_CALL(protocol_, readListEnd(Ref(buffer_))).WillOnce(Return(true));

  expectFieldEnd();
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());
  const ThriftField& field = *thrift_obj.fields().front();
  EXPECT_EQ(1, field.fieldId());
  EXPECT_EQ(FieldType::List, field.fieldType());

  const ThriftListValue& nested = field.getValue().getValueTyped<ThriftListValue>();
  EXPECT_EQ(field_type, nested.elementType());
  EXPECT_EQ(2, nested.elements().size());
  for (auto& value : nested.elements()) {
    checkValue(field_type, *value);
  }
}

// Test parsing a struct with a single set field (struct -> set).
TEST_P(ThriftObjectImplValueTest, ParseNestedSetValue) {
  FieldType field_type = GetParam();

  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectFieldBegin(FieldType::Set, 1);

  EXPECT_CALL(protocol_, readSetBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& type, uint32_t& size) -> bool {
        type = field_type;
        size = 2;
        return true;
      }));
  expectValue(field_type);
  expectValue(field_type);
  EXPECT_CALL(protocol_, readSetEnd(Ref(buffer_))).WillOnce(Return(true));

  expectFieldEnd();
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());
  const ThriftField& field = *thrift_obj.fields().front();
  EXPECT_EQ(1, field.fieldId());
  EXPECT_EQ(FieldType::Set, field.fieldType());

  const ThriftSetValue& nested = field.getValue().getValueTyped<ThriftSetValue>();
  EXPECT_EQ(field_type, nested.elementType());
  EXPECT_EQ(2, nested.elements().size());
  for (auto& value : nested.elements()) {
    checkValue(field_type, *value);
  }
}

// Test parsing a struct with a single map field (struct -> map).
TEST_P(ThriftObjectImplValueTest, ParseNestedMapValue) {
  FieldType field_type = GetParam();

  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectFieldBegin(FieldType::Map, 1);

  EXPECT_CALL(protocol_, readMapBegin(Ref(buffer_), _, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& key_type, FieldType& value_type,
                           uint32_t& size) -> bool {
        key_type = field_type;
        value_type = FieldType::String;
        size = 2;
        return true;
      }));
  expectValue(field_type);
  expectValue(FieldType::String);
  expectValue(field_type);
  expectValue(FieldType::String);
  EXPECT_CALL(protocol_, readMapEnd(Ref(buffer_))).WillOnce(Return(true));

  expectFieldEnd();
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());
  const ThriftField& field = *thrift_obj.fields().front();
  EXPECT_EQ(1, field.fieldId());
  EXPECT_EQ(FieldType::Map, field.fieldType());

  const ThriftMapValue& nested = field.getValue().getValueTyped<ThriftMapValue>();
  EXPECT_EQ(field_type, nested.keyType());
  EXPECT_EQ(FieldType::String, nested.valueType());
  EXPECT_EQ(2, nested.elements().size());
  for (auto& value : nested.elements()) {
    checkValue(field_type, *value.first);
    checkValue(FieldType::String, *value.second);
  }
}

// Test a struct with a map -> list -> set -> map -> list -> set -> struct.
TEST_F(ThriftObjectImplTest, DeeplyNestedStruct) {
  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectFieldBegin(FieldType::Map, 1);

  EXPECT_CALL(protocol_, readMapBegin(Ref(buffer_), _, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& key_type, FieldType& value_type,
                           uint32_t& size) -> bool {
        key_type = FieldType::I32;
        value_type = FieldType::List;
        size = 1;
        return true;
      }));
  expectValue(FieldType::I32);
  EXPECT_CALL(protocol_, readListBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& elem_type, uint32_t& size) -> bool {
        elem_type = FieldType::Set;
        size = 1;
        return true;
      }));
  EXPECT_CALL(protocol_, readSetBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& elem_type, uint32_t& size) -> bool {
        elem_type = FieldType::Map;
        size = 1;
        return true;
      }));

  EXPECT_CALL(protocol_, readMapBegin(Ref(buffer_), _, _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& key_type, FieldType& value_type,
                           uint32_t& size) -> bool {
        key_type = FieldType::I32;
        value_type = FieldType::List;
        size = 1;
        return true;
      }));
  expectValue(FieldType::I32);
  EXPECT_CALL(protocol_, readListBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& elem_type, uint32_t& size) -> bool {
        elem_type = FieldType::Set;
        size = 1;
        return true;
      }));
  EXPECT_CALL(protocol_, readSetBegin(Ref(buffer_), _, _))
      .WillOnce(Invoke([&](Buffer::Instance&, FieldType& elem_type, uint32_t& size) -> bool {
        elem_type = FieldType::Struct;
        size = 1;
        return true;
      }));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectField(FieldType::I64, 100);
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readSetEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readListEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMapEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readSetEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readListEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMapEnd(Ref(buffer_))).WillOnce(Return(true));

  expectFieldEnd();
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());

  EXPECT_EQ(FieldType::Map, thrift_obj.fields().front()->fieldType());
  const ThriftMapValue& map_value =
      thrift_obj.fields().front()->getValue().getValueTyped<ThriftMapValue>();
  EXPECT_EQ(1, map_value.elements().size());

  const ThriftListValue& list_value =
      map_value.elements().front().second->getValueTyped<ThriftListValue>();
  EXPECT_EQ(1, list_value.elements().size());

  const ThriftSetValue& set_value = list_value.elements().front()->getValueTyped<ThriftSetValue>();
  EXPECT_EQ(1, set_value.elements().size());

  const ThriftMapValue& map_value2 = set_value.elements().front()->getValueTyped<ThriftMapValue>();
  EXPECT_EQ(1, map_value2.elements().size());

  const ThriftListValue& list_value2 =
      map_value2.elements().front().second->getValueTyped<ThriftListValue>();
  EXPECT_EQ(1, list_value2.elements().size());

  const ThriftSetValue& set_value2 =
      list_value2.elements().front()->getValueTyped<ThriftSetValue>();
  EXPECT_EQ(1, set_value2.elements().size());

  const ThriftStructValue& struct_value =
      set_value2.elements().front()->getValueTyped<ThriftStructValue>();
  EXPECT_EQ(1, struct_value.fields().size());

  EXPECT_EQ(5, struct_value.fields().front()->getValue().getValueTyped<int64_t>());
}

// Tests when caller requests wrong value type.
TEST_F(ThriftObjectImplTest, WrongValueType) {
  ThriftObjectImpl thrift_obj(transport_, protocol_);

  InSequence s;
  EXPECT_CALL(transport_, decodeFrameStart(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageBegin(Ref(buffer_), _)).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readStructBegin(Ref(buffer_), _)).WillOnce(Return(true));
  expectField(FieldType::String, 1);
  expectStopField();
  EXPECT_CALL(protocol_, readStructEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(protocol_, readMessageEnd(Ref(buffer_))).WillOnce(Return(true));
  EXPECT_CALL(transport_, decodeFrameEnd(Ref(buffer_))).WillOnce(Return(true));

  EXPECT_TRUE(thrift_obj.onData(buffer_));
  EXPECT_EQ(1, thrift_obj.fields().size());

  const ThriftValue& value = thrift_obj.fields().front()->getValue();
  EXPECT_THROW_WITH_MESSAGE(value.getValueTyped<int32_t>(), EnvoyException,
                            fmt::format("expected field type {}, got {}",
                                        static_cast<int>(FieldType::I32),
                                        static_cast<int>(FieldType::String)));
}

} // Namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
