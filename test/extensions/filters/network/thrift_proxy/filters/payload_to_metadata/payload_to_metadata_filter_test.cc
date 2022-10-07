#include <string>

#include "source/common/common/base64.h"
#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

namespace {

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_NE(obj.fields().find(entry.first), obj.fields().end());
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqNum, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_NE(obj.fields().find(entry.first), obj.fields().end());
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}

} // namespace

using namespace Envoy::Extensions::NetworkFilters;

class PayloadToMetadataTest : public testing::Test {
public:
  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
        PayloadToMetadata proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    const auto& filter_config = std::make_shared<Config>(proto_config);
    filter_ = std::make_shared<PayloadToMetadataFilter>(filter_config);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, downstreamTransportType()).WillOnce(Return(transport_));
    EXPECT_CALL(decoder_callbacks_, downstreamProtocolType()).WillOnce(Return(protocol_));
  }

  // Request payload
  // {
  //   first_field: 1,
  //   second_field: "two",
  //   third_field: {
  //     f1: true,
  //     f2: 2,
  //     ...
  //     f6: 6,
  //     f7: "seven",
  //     f8: map,
  //     f9: list,
  //     f10: set
  //   }
  // }
  void writeMessage(MessageMetadata& metadata, Buffer::Instance& buffer, std::string string_value = "two") {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    metadata.setProtocol(protocol_);
    metadata.setMethodName("foo");
    metadata.setMessageType(MessageType::Call);
    metadata.setSequenceId(0);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "wrapper");
    proto->writeFieldBegin(msg, "first_field", FieldType::I64, 1);
    proto->writeInt64(msg, 1);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "second_field", FieldType::String, 2);
    proto->writeString(msg, string_value);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "third_field", FieldType::Struct, 3);

    proto->writeStructBegin(msg, "payload");
    proto->writeFieldBegin(msg, "f1", FieldType::Bool, 1);
    proto->writeBool(msg, true);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f2", FieldType::Byte, 2);
    proto->writeByte(msg, 2);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f3", FieldType::Double, 3);
    proto->writeDouble(msg, 3.0);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f4", FieldType::I16, 4);
    proto->writeInt16(msg, 4);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f5", FieldType::I32, 5);
    proto->writeInt32(msg, 5);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f6", FieldType::I64, 6);
    proto->writeInt64(msg, 6);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f7", FieldType::String, 7);
    proto->writeString(msg, "seven");
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f8", FieldType::Map, 8);
    proto->writeMapBegin(msg, FieldType::I32, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeInt32(msg, 8);
    proto->writeMapEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f9", FieldType::List, 9);
    proto->writeListBegin(msg, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeListEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f10", FieldType::Set, 10);
    proto->writeSetBegin(msg, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeSetEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // payload stop field
    proto->writeStructEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // wrapper stop field
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(transport_).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  NiceMock<ThriftProxy::ThriftFilters::MockDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info_;
  std::shared_ptr<PayloadToMetadataFilter> filter_;
  const ProtocolType protocol_{ProtocolType::Binary};
  const TransportType transport_{TransportType::Header};
};

TEST_F(PayloadToMetadataTest, MatchFirstLayerString) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerString) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f7
        id: 7
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "seven"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// TODO
// multiple rule tests
// num -> string
// num -> regex -> string
// list < protobuf
// list > protobuf
// list is sibling of protobuf

TEST_F(PayloadToMetadataTest, DefaultNamespaceTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      key: present
    on_missing:
      key: missing
      value: unknown
)EOF";
  initializeFilter(request_config_yaml);
  std::map<std::string, std::string> expected = {{"present", "two"}};
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.thrift.payload_to_metadata", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, ReplaceValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
      value: bar
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "bar"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, SubstituteValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^(\\w+)$"
        substitution: "\\1 cents"
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "two cents"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, NoMatchSubstituteValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^(\\w+)xxxx$"
        substitution: "\\1 cents"
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::string value = "do not match";
  std::map<std::string, std::string> expected = {{"present", value}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data, value);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Test empty value doesn't get written to metadata.
TEST_F(PayloadToMetadataTest, SubstituteEmptyValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^hello (\\w+)?.*$"
        substitution: "\\1"
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::string value = "hello !!!!";
  std::map<std::string, std::string> expected = {{"present", value}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data, value);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Test the value gets written as a number.
TEST_F(PayloadToMetadataTest, NumberTypeTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: number
      type: NUMBER
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, int> expected = {{"number", 1}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqNum(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data, "1");
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Test the invalid value won't get written as a number.
TEST_F(PayloadToMetadataTest, BadNumberTypeTest) {
    const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: number
      type: NUMBER
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, int> expected = {{"number", 1}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data, "1");
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Set configured value when payload is missing.
TEST_F(PayloadToMetadataTest, SetMissingValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: too_big
      id: 100
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";
  std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Missing case is not executed when payload is present.
TEST_F(PayloadToMetadataTest, NoMissingWhenPayloadIsPresent) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// No payload value does not set any metadata.
TEST_F(PayloadToMetadataTest, EmptyPayloadValue) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  // empty payload on the field
  writeMessage(*metadata, data, "");
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

// Payload value too long does not set payload value as metadata.
TEST_F(PayloadToMetadataTest, PayloadValueTooLong) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();

  auto length = MAX_PAYLOAD_VALUE_LEN + 1;
  writeMessage(*metadata, data,  std::string(length, 'x'));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
