#include <string>

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

using ::testing::Return;

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

class PayloadToMetadataTest : public testing::Test,
                              public DecoderCallbacks,
                              public PassThroughDecoderEventHandler {
public:
  void initializeFilter(const std::string& yaml, bool expect_type_inquiry = true) {
    envoy::extensions::filters::network::thrift_proxy::filters::payload_to_metadata::v3::
        PayloadToMetadata proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    const auto& filter_config = std::make_shared<Config>(proto_config);
    filter_ = std::make_shared<PayloadToMetadataFilter>(filter_config);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    if (expect_type_inquiry) {
      EXPECT_CALL(decoder_callbacks_, downstreamProtocolType()).WillOnce(Return(protocol_));
    } else {
      EXPECT_CALL(decoder_callbacks_, downstreamProtocolType()).Times(0);
    }
  }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return true; }
  bool isRequest() const override { return false; }
  bool headerKeysPreserveCase() const override { return false; }

  // PassThroughDecoderEventHandler
  FilterStatus passthroughData(Buffer::Instance& data) override {
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
    return ThriftProxy::FilterStatus::Continue;
  }
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
    return ThriftProxy::FilterStatus::Continue;
  }

  // Request payload
  // {
  //   first_field: 1,
  //   second_field: "two" or string_value,
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
  void writeMessage(const std::string& string_value = "two",
                    const std::string& method_name = "foo") {
    Buffer::OwnedImpl buffer;
    auto metadata_ptr =
        std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
    auto& metadata = *metadata_ptr;

    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    metadata.setProtocol(protocol_);
    if (!method_name.empty()) {
      metadata.setMethodName(method_name);
    }
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

    TransportPtr transport = NamedTransportConfigFactory::getFactory(transport_).createTransport();
    transport->encodeFrame(buffer, metadata, msg);

    // Simulate the decoder events. Check PassThroughDecoderEventHandler.
    ProtocolPtr decoder_proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    TransportPtr decoder_transport =
        NamedTransportConfigFactory::getFactory(transport_).createTransport();
    DecoderPtr decoder = std::make_unique<Decoder>(*decoder_transport, *decoder_proto, *this);
    bool underflow = false;
    decoder->onData(buffer, underflow);
  }

  void writeMessageMultiStruct() {
    Buffer::OwnedImpl buffer;
    auto metadata_ptr =
        std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
    auto& metadata = *metadata_ptr;

    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    metadata.setProtocol(protocol_);
    metadata.setMethodName("foo");
    metadata.setMessageType(MessageType::Call);
    metadata.setSequenceId(0);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "wrapper");
    proto->writeFieldBegin(msg, "context", FieldType::Struct, 1);
    proto->writeStructBegin(msg, "context");

    proto->writeFieldBegin(msg, "source", FieldType::String, 1);
    proto->writeString(msg, "bar");
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "requestId", FieldType::String, 2);
    proto->writeString(msg, "42");
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // context stop field
    proto->writeStructEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "request", FieldType::Struct, 2);
    proto->writeStructBegin(msg, "request");

    proto->writeFieldBegin(msg, "baz", FieldType::String, 1);
    proto->writeString(msg, "qux");
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // request stop field
    proto->writeStructEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // wrapper stop field
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport = NamedTransportConfigFactory::getFactory(transport_).createTransport();
    transport->encodeFrame(buffer, metadata, msg);

    // Simulate the decoder events. Check PassThroughDecoderEventHandler.
    ProtocolPtr decoder_proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    TransportPtr decoder_transport =
        NamedTransportConfigFactory::getFactory(transport_).createTransport();
    DecoderPtr decoder = std::make_unique<Decoder>(*decoder_transport, *decoder_proto, *this);
    bool underflow = false;
    decoder->onData(buffer, underflow);
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

  const std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
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

  const std::map<std::string, std::string> expected = {{"present", "seven"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchFirstLayerNumber) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: first_field
      id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "1"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerBool) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f1
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "1"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerByte) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f2
        id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "2"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerDouble) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f3
        id: 3
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "3.000000"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerInt16) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f4
        id: 4
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "4"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerInt32) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f5
        id: 5
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "5"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MatchSecondLayerInt64) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: payload
      id: 3
      child:
        name: f6
        id: 6
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";

  const std::map<std::string, std::string> expected = {{"present", "6"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, EmptyMethodName) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: ""
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

  const std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MethodNameWithServicePrefix) {
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

  const std::map<std::string, std::string> expected = {{"present", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage("two", "service:foo");
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, WrongMethodNameWithServicePrefix) {
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

  initializeFilter(request_config_yaml, false);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage("two", "service:bar");
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, DoNotMatchServiceName) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - service_name: unknown
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

  initializeFilter(request_config_yaml, false);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

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
  const std::map<std::string, std::string> expected = {{"present", "two"}};
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.thrift.payload_to_metadata", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
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

  const std::map<std::string, std::string> expected = {{"present", "bar"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
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

  const std::map<std::string, std::string> expected = {{"present", "two cents"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
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
  const std::map<std::string, std::string> expected = {{"present", value}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
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

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
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
  const std::string value = "1";

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqNum(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
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

  const std::string value = "invalid";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
  filter_->onDestroy();
}

// Number to number test
TEST_F(PayloadToMetadataTest, NumberToNumberType) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: first_field
      id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: number
      type: NUMBER
)EOF";

  std::map<std::string, int> expected = {{"number", 1}};
  const std::string value = "1";

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqNum(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
  filter_->onDestroy();
}

// Number to number with regex test
TEST_F(PayloadToMetadataTest, NumberToNumberTypeWithRegex) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: first_field
      id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: number
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^(\\w+)$"
        substitution: "91\\1 "
      type: NUMBER
)EOF";

  std::map<std::string, int> expected = {{"number", 911}};
  const std::string value = "1";

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqNum(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage(value);
  filter_->onDestroy();
}

// Set configured value when payload is missing.
TEST_F(PayloadToMetadataTest, MissingValueInFirstLayer) {
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
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// Set configured value when payload is missing in the second layer.
TEST_F(PayloadToMetadataTest, MissingValueInSecondLayer) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
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
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// Set configured value when payload is missing in the third layer.
TEST_F(PayloadToMetadataTest, MissingValueInThirdLayer) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
        name: f1
        id: 1
        child:
          name: unknown
          id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// Perform on_missing while field selector points to a struct.
TEST_F(PayloadToMetadataTest, FieldSelectorPointsToStruct) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// Perform on_missing while field selector points to a map.
TEST_F(PayloadToMetadataTest, FieldSelectorPointsToMap) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
        name: f8
        id: 8
    on_present:
      metadata_namespace: envoy.lb
      key: present
    on_missing:
      metadata_namespace: envoy.lb
      key: missing
      value: unknown
)EOF";
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// Set configured value when payload is missing in the second layer.
TEST_F(PayloadToMetadataTest, PointToNonExistSecondLayer) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
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
  const std::map<std::string, std::string> expected = {{"missing", "unknown"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

// on_missing is not executed when payload is present.
TEST_F(PayloadToMetadataTest, NoApplyOnMissingWhenPayloadIsPresent) {
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

  writeMessage();
  filter_->onDestroy();
}

// on_present is not executed when payload is missing.
TEST_F(PayloadToMetadataTest, NoApplyOnPresentWhenPayloadIsPresent) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: too_big
      id: 100
    on_present:
      metadata_namespace: envoy.lb
      key: present
)EOF";

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
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

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  // empty payload on the field
  const std::string value = "";

  writeMessage(value);
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

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  auto length = MAX_PAYLOAD_VALUE_LEN + 1;
  const std::string value = std::string(length, 'x');

  writeMessage(value);
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MultipleRulesTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
  - method_name: bar
    field_selector:
      name: first_field
      id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: method_not_match
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
        name: f6
        id: 6
    on_present:
      metadata_namespace: envoy.lb
      key: six
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
        name: f7
        id: 7
    on_present:
      metadata_namespace: envoy.lb
      key: seven
)EOF";

  const std::map<std::string, std::string> expected = {
      {"present", "two"}, {"six", "6"}, {"seven", "seven"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MultipleRulesInSamePath) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present2
  - method_name: bar
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: method_not_match
  - method_name: foo
    field_selector:
      name: third_field
      id: 3
      child:
        name: f6
        id: 6
    on_present:
      metadata_namespace: envoy.lb
      key: six
  - method_name: bar
    field_selector:
      name: third_field
      id: 3
      child:
        name: f6
        id: 6
    on_present:
      metadata_namespace: envoy.lb
      key: method_not_match_again
)EOF";

  const std::map<std::string, std::string> expected = {
      {"present", "two"}, {"present2", "two"}, {"six", "6"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MultipleRulesInSamePathFirstRuleUnmatched) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: not_foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present
  - method_name: foo
    field_selector:
      name: second_field
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: present2
)EOF";

  const std::map<std::string, std::string> expected = {{"present2", "two"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessage();
  filter_->onDestroy();
}

TEST_F(PayloadToMetadataTest, MultipleRulesOnMultiStruct) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: unmatched_foo
    field_selector:
      name: request
      id: 2
      child:
        name: baz
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: baz
  - method_name: unmatched_foo2
    field_selector:
      name: request
      id: 2
      child:
        name: baz
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: baz
  - method_name: foo
    field_selector:
      name: request
      id: 2
      child:
        name: baz
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF";

  const std::map<std::string, std::string> expected = {{"baz", "qux"}};

  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  writeMessageMultiStruct();
  filter_->onDestroy();
}

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
