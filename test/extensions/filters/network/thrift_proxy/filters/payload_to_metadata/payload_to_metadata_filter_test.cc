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
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqNum, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqValue, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_TRUE(TestUtility::protoEqual(obj.fields().at(entry.first), entry.second));
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

  void writeMessage(MessageMetadata& metadata, Buffer::Instance& buffer) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    metadata.setProtocol(protocol_);
    metadata.setMethodName("foo");
    metadata.setMessageType(MessageType::Call);
    metadata.setSequenceId(0);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "response");
    proto->writeFieldBegin(msg, "success", FieldType::String, 0);
    proto->writeString(msg, "field");
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
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

//  Set configured value when payload is missing.
TEST_F(PayloadToMetadataTest, SetMissingValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: info
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: unknown
)EOF";
  initializeFilter(request_config_yaml);
  std::map<std::string, std::string> expected = {{"default", "unknown"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));

  Buffer::OwnedImpl data;
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  writeMessage(*metadata, data);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(metadata));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->passthroughData(data));
  filter_->onDestroy();
}


} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
