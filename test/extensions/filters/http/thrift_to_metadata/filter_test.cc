#include <memory>

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/thrift_to_metadata/filter.h"
#include "source/extensions/filters/http/well_known_names.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"

#include "test/common/buffer/utility.h"
#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapNumEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}
// class FilterTest : public testing::Test {
class FilterTest : public testing::TestWithParam<std::tuple<TransportType, ProtocolType>> {
public:
  FilterTest() = default;

  const std::string config_yaml_ = R"EOF(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
  on_missing:
    metadata_namespace: envoy.lb
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    metadata_namespace: envoy.lb
    key: transport
  on_missing:
    metadata_namespace: envoy.lb
    key: transport
    value: "unknown"
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.lb
    key: request_message_type
  on_missing:
    metadata_namespace: envoy.lb
    key: request_message_type
    value: "unknown"
- field: METHOD_NAME
  on_present:
    metadata_namespace: envoy.lb
    key: method_name
  on_missing:
    metadata_namespace: envoy.lb
    key: method_name
    value: "unknown"
response_rules:
- field: MESSAGE_TYPE
  on_present:
    key: response_message_type
  on_missing:
    key: response_message_type
    value: "unknown"
- field: REPLY_TYPE
  on_present:
    key: response_reply_type
  on_missing:
    key: response_reply_type
    value: "unknown"
- field: PROTOCOL
  on_present:
    key: protocol
  on_missing:
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    key: transport
  on_missing:
    key: transport
    value: "unknown"
- field: METHOD_NAME
  on_present:
    key: method_name
  on_missing:
    key: method_name
    value: "unknown"
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, *scope_.rootScope());
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  uint64_t getCounterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(scope_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  void testRequest(NetworkFilters::ThriftProxy::TransportType transport_type,
                   NetworkFilters::ThriftProxy::ProtocolType protocol_type,
                   NetworkFilters::ThriftProxy::MessageType message_type,
                   const std::map<std::string, std::string>& expected_metadata) {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, false));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

    Buffer::OwnedImpl buffer;
    writeMessage(buffer, transport_type, protocol_type, message_type);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
  }

  void testResponse(NetworkFilters::ThriftProxy::TransportType transport_type,
                    NetworkFilters::ThriftProxy::ProtocolType protocol_type,
                    NetworkFilters::ThriftProxy::MessageType message_type,
                    NetworkFilters::ThriftProxy::ReplyType reply_type,
                    const std::map<std::string, std::string>& expected_metadata) {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers_, false));
    EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                                 MapEq(expected_metadata)));

    Buffer::OwnedImpl buffer;
    writeMessage(buffer, transport_type, protocol_type, message_type, reply_type);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 1);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
    EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
  }

  void
  writeMessage(Buffer::OwnedImpl& buffer, NetworkFilters::ThriftProxy::TransportType transport_type,
               NetworkFilters::ThriftProxy::ProtocolType protocol_type,
               NetworkFilters::ThriftProxy::MessageType message_type,
               absl::optional<NetworkFilters::ThriftProxy::ReplyType> reply_type = absl::nullopt) {
    Buffer::OwnedImpl proto_buffer;
    ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
    ProtocolPtr protocol = createProtocol(protocol_type);
    protocol_converter->initProtocolConverter(*protocol, proto_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    metadata->setProtocol(protocol_type);
    metadata->setMethodName(method_name_);
    metadata->setMessageType(message_type);
    metadata->setSequenceId(1234);

    protocol_converter->messageBegin(metadata);
    protocol_converter->structBegin("");
    int16_t field_id = 0;
    FieldType field_type_string = FieldType::String;
    FieldType field_type_struct = FieldType::Struct;
    FieldType field_type_stop = FieldType::Stop;
    if (message_type == MessageType::Reply || message_type == MessageType::Exception) {
      if (reply_type.value() == ReplyType::Success) {
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("value1");
        protocol_converter->fieldEnd();
      } else {
        // successful response struct in field id 0, error (IDL exception) in field id greater than
        // 0
        field_id = 2;
        protocol_converter->fieldBegin("", field_type_struct, field_id);
        protocol_converter->structBegin("");
        field_id = 1;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("err");
        protocol_converter->fieldEnd();
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_stop, field_id);
        protocol_converter->structEnd();
        protocol_converter->fieldEnd();
      }
    }
    field_id = 0;
    protocol_converter->fieldBegin("", field_type_stop, field_id);
    protocol_converter->structEnd();
    protocol_converter->messageEnd();

    TransportPtr transport = createTransport(transport_type);
    transport->encodeFrame(buffer, *metadata, proto_buffer);
  }

  TransportPtr createTransport(NetworkFilters::ThriftProxy::TransportType transport) {
    return NamedTransportConfigFactory::getFactory(transport).createTransport();
  }

  ProtocolPtr createProtocol(NetworkFilters::ThriftProxy::ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }

  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
  const std::string method_name_{"foo"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/Service"}, {":method", "POST"}, {"Content-Type", "application/x-thrift"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"Content-Type", "application/x-thrift"}};
};

INSTANTIATE_TEST_SUITE_P(
    RequestCombinations, FilterTest,
    testing::Combine(testing::Values(TransportType::Unframed, TransportType::Framed,
                                     TransportType::Header),
                     testing::Values(ProtocolType::Binary, ProtocolType::Compact)));

TEST_P(FilterTest, CallRequestSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_);
  testRequest(transport_type, protocol_type, message_type,
              {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
               {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
               {"request_message_type", MessageTypeNames::get().fromType(message_type)},
               {"method_name", method_name_}});
}

TEST_P(FilterTest, CallRequestSuccessWithNumberField) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;
  initializeFilter(R"EOF(
request_rules:
- field: SEQUENCE_ID
  on_present:
    metadata_namespace: envoy.lb
    key: sequence_id
- field: HEADER_FLAGS
  on_present:
    metadata_namespace: envoy.lb
    key: header_flags
  on_missing:
    metadata_namespace: envoy.lb
    key: header_flags
    value: 999
)EOF");
  const std::map<std::string, int>& expected_metadata = {
      {"sequence_id", 1234}, {"header_flags", transport_type == TransportType::Header ? 0 : 999}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapNumEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_P(FilterTest, OnewayRequestSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Oneway;

  initializeFilter(config_yaml_);
  testRequest(transport_type, protocol_type, message_type,
              {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
               {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
               {"request_message_type", MessageTypeNames::get().fromType(message_type)},
               {"method_name", method_name_}});
}

TEST_P(FilterTest, ReplyResponseSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  testResponse(transport_type, protocol_type, message_type, ReplyType::Success,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(message_type)},
                {"method_name", method_name_},
                {"response_reply_type", "success"}});
}

TEST_P(FilterTest, ExceptionResponseSuccess) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Exception;

  initializeFilter(config_yaml_);
  testResponse(transport_type, protocol_type, message_type, ReplyType::Success,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(message_type)},
                {"method_name", method_name_},
                // Exception doesn't have a reply type.
                {"response_reply_type", "unknown"}});
}

TEST_P(FilterTest, ReplyResponseError) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  testResponse(transport_type, protocol_type, message_type, ReplyType::Error,
               {{"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
                {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
                {"response_message_type", MessageTypeNames::get().fromType(message_type)},
                {"method_name", method_name_},
                {"response_reply_type", "error"}});
}

TEST_P(FilterTest, ExplictProtocolTransport) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_ + fmt::format("protocol: {0}\ntransport: {1}\n",
                                              ProtocolNames::get().fromType(protocol_type),
                                              TransportNames::get().fromType(transport_type)));
  testRequest(transport_type, protocol_type, message_type,
              {{"protocol", ProtocolNames::get().fromType(protocol_type)},
               {"transport", TransportNames::get().fromType(transport_type)},
               {"request_message_type", MessageTypeNames::get().fromType(message_type)},
               {"method_name", method_name_}});
}

TEST_F(FilterTest, NoRequestBody) {
  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"request_message_type", "unknown"},
      {"method_name", "unknown"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, NoResponseBody) {
  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_P(FilterTest, IncompleteRequest) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"request_message_type", "unknown"},
      {"method_name", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, IncompleteResponse) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type, ReplyType::Success);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, IncompleteRequestWithTrailer) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"request_message_type", "unknown"},
      {"method_name", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  filter_->decodeComplete();

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, IncompleteRequestWithEarlyComplete) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"request_message_type", "unknown"},
      {"method_name", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));

  filter_->decodeComplete();

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, IncompleteResponseWithTrailer) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type, ReplyType::Success);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));

  Http::TestResponseTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
  filter_->encodeComplete();

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, IncompleteResponseEarlyComplete) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type, ReplyType::Success);
  Buffer::OwnedImpl buffer;
  // incomplete message
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));

  filter_->encodeComplete();

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, MalformedRequest) {
  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"request_message_type", "unknown"},
      {"method_name", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer{"malformed thrift body"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 1);
}

TEST_F(FilterTest, MalformedResponse) {
  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer{"malformed thrift body"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 1);
}

TEST_F(FilterTest, ApplicationExceptionResponse) {
  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", "unknown"},
      {"transport", "unknown"},
      {"response_message_type", "unknown"},
      {"method_name", "unknown"},
      {"response_reply_type", "unknown"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  // Response with unknown transform
  Buffer::addSeq(buffer, {
                             0x00, 0x00, 0x00, 0x64, // header: 100 bytes
                             0x0f, 0xff, 0x00, 0x00, // magic, flags
                             0x00, 0x00, 0x00, 0x01, // sequence id
                             0x00, 0x01, 0x00, 0x02, // header size 4, binary proto, 2 transforms
                             0x01, 0x02, 0x00, 0x00, // transforms: 1, 2; padding
                         });

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 1);
}

TEST_P(FilterTest, DecodeTwoDataStreams) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"request_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type);

  Buffer::OwnedImpl buffer;
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));

  buffer.drain(buffer.length());
  buffer.move(whole_message);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_P(FilterTest, EncodeTwoDataStreams) {
  const auto [transport_type, protocol_type] = GetParam();
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_);
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"response_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_},
      {"response_reply_type", "success"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl whole_message;
  writeMessage(whole_message, transport_type, protocol_type, message_type, ReplyType::Success);

  Buffer::OwnedImpl buffer;
  buffer.move(whole_message, whole_message.length() / 2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));

  buffer.drain(buffer.length());
  buffer.move(whole_message);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, RequestBodyWithResponseRule) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Call;

  initializeFilter(R"EOF(
response_rules:
- field: MESSAGE_TYPE
  on_present:
    key: response_message_type
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, ResponseBodyWithRequestRule) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Reply;

  initializeFilter(R"EOF(
request_rules:
- field: MESSAGE_TYPE
  on_present:
    key: request_message_type
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type, ReplyType::Success);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, RequestHeaderWithResponseRule) {
  initializeFilter(R"EOF(
response_rules:
- field: MESSAGE_TYPE
  on_present:
    key: response_message_type
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, ResponseHeaderWithRequestRule) {
  initializeFilter(R"EOF(
request_rules:
- field: MESSAGE_TYPE
  on_present:
    key: request_message_type
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, NoApplyOnMissingWhenMetadataIsPresent) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Call;

  initializeFilter(R"EOF(
request_rules:
- field: MESSAGE_TYPE
  on_missing:
    key: response_message_type
    value: "unknown"
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, NoRequestContentType) {
  initializeFilter(config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_headers{{":path", "/ping"}, {":method", "GET"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, NoResponseContentType) {
  initializeFilter(config_yaml_);

  Http::TestResponseHeaderMapImpl mismatched_headers{{":status", "200"}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, MismatchedRequestContentType) {
  initializeFilter(config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/not-a-thrift"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.Mismatch_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, MismatchedResponseContentType) {
  initializeFilter(config_yaml_);

  Http::TestResponseHeaderMapImpl mismatched_headers{{":status", "200"},
                                                     {"Content-Type", "application/not-a-thrift"}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, RequestAllowEmptyContentType) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_ + "allow_empty_content_type: true\n");
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"request_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_}};

  Http::TestRequestHeaderMapImpl headers_without_content_type{{":path", "/ping"},
                                                              {":method", "GET"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(headers_without_content_type, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, ResponseAllowEmptyContentType) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_ + "allow_empty_content_type: true\n");
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"response_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_},
      {"response_reply_type", "success"}};

  Http::TestResponseHeaderMapImpl headers_without_content_type{{":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(headers_without_content_type, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type, ReplyType::Success);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeAccepted) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Call;

  initializeFilter(config_yaml_ + "allow_content_types:\n  - \"application/fun-thrift\"\n");
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"request_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_}};

  Http::TestRequestHeaderMapImpl headers_without_content_type{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/fun-thrift"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(headers_without_content_type, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, CustomResponseAllowContentTypeAccepted) {
  TransportType transport_type = TransportType::Unframed;
  ProtocolType protocol_type = ProtocolType::Binary;
  MessageType message_type = MessageType::Reply;

  initializeFilter(config_yaml_ + "allow_content_types:\n  - \"application/fun-thrift\"\n");
  const std::map<std::string, std::string>& expected_metadata = {
      {"protocol", ProtocolNames::get().fromType(protocol_type) + "(auto)"},
      {"transport", TransportNames::get().fromType(transport_type) + "(auto)"},
      {"response_message_type", MessageTypeNames::get().fromType(message_type)},
      {"method_name", method_name_},
      {"response_reply_type", "success"}};

  Http::TestResponseHeaderMapImpl headers_without_content_type{
      {":status", "200"}, {"Content-Type", "application/fun-thrift"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(headers_without_content_type, false));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().ThriftToMetadata,
                                               MapEq(expected_metadata)));

  Buffer::OwnedImpl buffer;
  writeMessage(buffer, transport_type, protocol_type, message_type, ReplyType::Success);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeRejected) {
  initializeFilter(config_yaml_ + "allow_content_types:\n  - \"application/fun-thrift\"\n");

  Http::TestRequestHeaderMapImpl mismatched_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/x-thrift"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.Mismatch_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.rq.invalid_thrift_body"), 0);
}

TEST_F(FilterTest, CustomResponseAllowContentTypeRejected) {
  initializeFilter(config_yaml_ + "allow_content_types:\n  - \"application/fun-thrift\"\n");

  Http::TestResponseHeaderMapImpl mismatched_headers{{":status", "200"},
                                                     {"Content-Type", "application/x-thrift"}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(mismatched_headers, true));

  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("thrift_to_metadata.resp.invalid_thrift_body"), 0);
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
