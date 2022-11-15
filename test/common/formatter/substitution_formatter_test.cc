#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/stream_id_provider_impl.h"

#include "test/common/formatter/command_extension.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Formatter {
namespace {

class TestSerializedUnknownFilterState : public StreamInfo::FilterState::Object {
public:
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto any = std::make_unique<ProtobufWkt::Any>();
    any->set_type_url("UnknownType");
    any->set_value("\xde\xad\xbe\xef");
    return any;
  }
};

class TestSerializedStructFilterState : public StreamInfo::FilterState::Object {
public:
  TestSerializedStructFilterState() : use_struct_(true) {
    (*struct_.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  }

  explicit TestSerializedStructFilterState(const ProtobufWkt::Struct& s) : use_struct_(true) {
    struct_.CopyFrom(s);
  }

  explicit TestSerializedStructFilterState(std::chrono::seconds seconds) {
    duration_.set_seconds(seconds.count());
  }

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    if (use_struct_) {
      auto s = std::make_unique<ProtobufWkt::Struct>();
      s->CopyFrom(struct_);
      return s;
    }

    auto d = std::make_unique<ProtobufWkt::Duration>();
    d->CopyFrom(duration_);
    return d;
  }

private:
  const bool use_struct_{false};
  ProtobufWkt::Struct struct_;
  ProtobufWkt::Duration duration_;
};

// Class used to test serializeAsString and serializeAsProto of FilterState
class TestSerializedStringFilterState : public StreamInfo::FilterState::Object {
public:
  TestSerializedStringFilterState(std::string str) : raw_string_(str) {}
  absl::optional<std::string> serializeAsString() const override {
    return raw_string_ + " By PLAIN";
  }
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::StringValue>();
    message->set_value(raw_string_ + " By TYPED");
    return message;
  }

private:
  std::string raw_string_;
};

// Test tests multiple versions of variadic template method parseSubcommand
// extracting tokens.
TEST(SubstitutionFormatParser, commandParser) {
  std::vector<absl::string_view> tokens;
  std::string token1;

  std::string command = "item1";
  SubstitutionFormatParser::parseSubcommand(command, ':', token1);
  ASSERT_EQ(token1, "item1");

  std::string token2;
  command = "item1:item2";
  SubstitutionFormatParser::parseSubcommand(command, ':', token1, token2);
  ASSERT_EQ(token1, "item1");
  ASSERT_EQ(token2, "item2");

  // Three tokens.
  std::string token3;
  command = "item1?item2?item3";
  SubstitutionFormatParser::parseSubcommand(command, '?', token1, token2, token3);
  ASSERT_EQ(token1, "item1");
  ASSERT_EQ(token2, "item2");
  ASSERT_EQ(token3, "item3");

  // Command string has 4 tokens but 3 are expected.
  // The first 3 will be read, the fourth will be ignored.
  command = "item1?item2?item3?item4";
  SubstitutionFormatParser::parseSubcommand(command, '?', token1, token2, token3);
  ASSERT_EQ(token1, "item1");
  ASSERT_EQ(token2, "item2");
  ASSERT_EQ(token3, "item3");

  // Command string has 2 tokens but 3 are expected.
  // The third extracted token should be empty.
  command = "item1?item2";
  token3.erase();
  SubstitutionFormatParser::parseSubcommand(command, '?', token1, token2, token3);
  ASSERT_EQ(token1, "item1");
  ASSERT_EQ(token2, "item2");
  ASSERT_TRUE(token3.empty());

  // Command string has 4 tokens. Get first 2 into the strings
  // and remaining 2 into a vector of strings.
  command = "item1?item2?item3?item4";
  std::vector<std::string> bucket;
  SubstitutionFormatParser::parseSubcommand(command, '?', token1, token2, bucket);
  ASSERT_EQ(token1, "item1");
  ASSERT_EQ(token2, "item2");
  ASSERT_EQ(bucket.size(), 2);
  ASSERT_EQ(bucket.at(0), "item3");
  ASSERT_EQ(bucket.at(1), "item4");
}

TEST(SubstitutionFormatUtilsTest, protocolToString) {
  EXPECT_EQ("HTTP/1.0",
            SubstitutionFormatUtils::protocolToString(Http::Protocol::Http10).value().get());
  EXPECT_EQ("HTTP/1.1",
            SubstitutionFormatUtils::protocolToString(Http::Protocol::Http11).value().get());
  EXPECT_EQ("HTTP/2",
            SubstitutionFormatUtils::protocolToString(Http::Protocol::Http2).value().get());
  EXPECT_EQ(absl::nullopt, SubstitutionFormatUtils::protocolToString({}));
}

TEST(SubstitutionFormatUtilsTest, protocolToStringOrDefault) {
  EXPECT_EQ("HTTP/1.0", SubstitutionFormatUtils::protocolToStringOrDefault(Http::Protocol::Http10));
  EXPECT_EQ("HTTP/1.1", SubstitutionFormatUtils::protocolToStringOrDefault(Http::Protocol::Http11));
  EXPECT_EQ("HTTP/2", SubstitutionFormatUtils::protocolToStringOrDefault(Http::Protocol::Http2));
  EXPECT_EQ("-", SubstitutionFormatUtils::protocolToStringOrDefault({}));
}

TEST(SubstitutionFormatterTest, plainStringFormatter) {
  PlainStringFormatter formatter("plain");
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  EXPECT_EQ("plain", formatter.format(request_headers, response_headers, response_trailers,
                                      stream_info, body));
  EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                    stream_info, body),
              ProtoEq(ValueUtil::stringValue("plain")));
}

TEST(SubstitutionFormatterTest, plainNumberFormatter) {
  PlainNumberFormatter formatter(400);
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  EXPECT_EQ("400", formatter.format(request_headers, response_headers, response_trailers,
                                    stream_info, body));
  EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                    stream_info, body),
              ProtoEq(ValueUtil::numberValue(400)));
}

TEST(SubstitutionFormatterTest, streamInfoFormatter) {
  EXPECT_THROW(StreamInfoFormatter formatter("unknown_field"), EnvoyException);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;
  MockTimeSystem time_system;
  auto& upstream_timing = stream_info.upstream_info_->upstreamTiming();

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    EXPECT_EQ(absl::nullopt, request_duration_format.format(request_headers, response_headers,
                                                            response_trailers, stream_info, body));
    EXPECT_THAT(request_duration_format.formatValue(request_headers, response_headers,
                                                    response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(5000000))));
    stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);
    EXPECT_EQ("5", request_duration_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(request_duration_format.formatValue(request_headers, response_headers,
                                                    response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(5.0)));
  }

  {
    StreamInfoFormatter request_tx_duration_format("REQUEST_TX_DURATION");
    EXPECT_EQ(absl::nullopt,
              request_tx_duration_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(request_tx_duration_format.formatValue(request_headers, response_headers,
                                                       response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter request_tx_duration_format("REQUEST_TX_DURATION");
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(15000000))));
    upstream_timing.onLastUpstreamTxByteSent(time_system);
    EXPECT_EQ("15", request_tx_duration_format.format(request_headers, response_headers,
                                                      response_trailers, stream_info, body));
    EXPECT_THAT(request_tx_duration_format.formatValue(request_headers, response_headers,
                                                       response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(15.0)));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    EXPECT_EQ(absl::nullopt, response_duration_format.format(request_headers, response_headers,
                                                             response_trailers, stream_info, body));
    EXPECT_THAT(response_duration_format.formatValue(request_headers, response_headers,
                                                     response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(10000000))));
    upstream_timing.onFirstUpstreamRxByteReceived(time_system);
    EXPECT_EQ("10", response_duration_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(response_duration_format.formatValue(request_headers, response_headers,
                                                     response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(10.0)));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    EXPECT_EQ(absl::nullopt, ttlb_duration_format.format(request_headers, response_headers,
                                                         response_trailers, stream_info, body));
    EXPECT_THAT(ttlb_duration_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(25000000))));
    stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);

    EXPECT_EQ("15", ttlb_duration_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(ttlb_duration_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(15.0)));
  }

  {
    StreamInfoFormatter handshake_duration_format("DOWNSTREAM_HANDSHAKE_DURATION");

    EXPECT_EQ(absl::nullopt,
              handshake_duration_format.format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
    EXPECT_THAT(handshake_duration_format.formatValue(request_headers, response_headers,
                                                      response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter handshake_duration_format("DOWNSTREAM_HANDSHAKE_DURATION");

    EXPECT_CALL(time_system, monotonicTime)
        .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(25000000))));
    stream_info.downstream_timing_.onDownstreamHandshakeComplete(time_system);

    EXPECT_EQ("25", handshake_duration_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info, body));
    EXPECT_THAT(handshake_duration_format.formatValue(request_headers, response_headers,
                                                      response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(25.0)));
  }

  {
    StreamInfoFormatter bytes_received_format("BYTES_RECEIVED");
    EXPECT_CALL(stream_info, bytesReceived()).WillRepeatedly(Return(1));
    EXPECT_EQ("1", bytes_received_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(bytes_received_format.formatValue(request_headers, response_headers,
                                                  response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfoFormatter attempt_count_format("UPSTREAM_REQUEST_ATTEMPT_COUNT");
    absl::optional<uint32_t> attempt_count{3};
    EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(attempt_count));
    EXPECT_EQ("3", attempt_count_format.format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
    EXPECT_THAT(attempt_count_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(3.0)));
  }

  {
    StreamInfoFormatter attempt_count_format("UPSTREAM_REQUEST_ATTEMPT_COUNT");
    absl::optional<uint32_t> attempt_count;
    EXPECT_CALL(stream_info, attemptCount()).WillRepeatedly(Return(attempt_count));
    EXPECT_EQ("0", attempt_count_format.format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
    EXPECT_THAT(attempt_count_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(0.0)));
  }

  {
    StreamInfo::BytesMeterSharedPtr upstream_bytes_meter{
        std::make_shared<StreamInfo::BytesMeter>()};
    upstream_bytes_meter->addWireBytesReceived(1);
    StreamInfoFormatter wire_bytes_received_format("UPSTREAM_WIRE_BYTES_RECEIVED");
    EXPECT_CALL(stream_info, getUpstreamBytesMeter())
        .WillRepeatedly(ReturnRef(upstream_bytes_meter));
    EXPECT_EQ("1", wire_bytes_received_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info, body));
    EXPECT_THAT(wire_bytes_received_format.formatValue(request_headers, response_headers,
                                                       response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfoFormatter protocol_format("PROTOCOL");
    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));
    EXPECT_EQ("HTTP/1.1", protocol_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(protocol_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("HTTP/1.1")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter protocol_format("UPSTREAM_PROTOCOL");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(absl::nullopt, protocol_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(protocol_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter protocol_format("UPSTREAM_PROTOCOL");
    EXPECT_EQ(absl::nullopt, protocol_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(protocol_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter protocol_format("UPSTREAM_PROTOCOL");
    Http::Protocol protocol = Http::Protocol::Http2;
    stream_info.upstreamInfo()->setUpstreamProtocol(protocol);
    EXPECT_EQ("HTTP/2", protocol_format.format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
    EXPECT_THAT(protocol_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("HTTP/2")));
  }

  {
    StreamInfoFormatter response_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("200", response_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(response_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::numberValue(200.0)));
  }

  {
    StreamInfoFormatter response_code_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code;
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("0", response_code_format.format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
    EXPECT_THAT(response_code_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(0.0)));
  }

  {
    StreamInfoFormatter response_format("RESPONSE_CODE_DETAILS");
    absl::optional<std::string> rc_details;
    EXPECT_CALL(stream_info, responseCodeDetails()).WillRepeatedly(ReturnRef(rc_details));
    EXPECT_EQ(absl::nullopt, response_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(response_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter response_code_format("RESPONSE_CODE_DETAILS");
    absl::optional<std::string> rc_details{"via_upstream"};
    EXPECT_CALL(stream_info, responseCodeDetails()).WillRepeatedly(ReturnRef(rc_details));
    EXPECT_EQ("via_upstream", response_code_format.format(request_headers, response_headers,
                                                          response_trailers, stream_info, body));
    EXPECT_THAT(response_code_format.formatValue(request_headers, response_headers,
                                                 response_trailers, stream_info, body),
                ProtoEq(ValueUtil::stringValue("via_upstream")));
  }

  {
    StreamInfoFormatter termination_details_format("CONNECTION_TERMINATION_DETAILS");
    absl::optional<std::string> details;
    EXPECT_CALL(stream_info, connectionTerminationDetails()).WillRepeatedly(ReturnRef(details));
    EXPECT_EQ(absl::nullopt,
              termination_details_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(termination_details_format.formatValue(request_headers, response_headers,
                                                       response_trailers, stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter termination_details_format("CONNECTION_TERMINATION_DETAILS");
    absl::optional<std::string> details{"access_denied"};
    EXPECT_CALL(stream_info, connectionTerminationDetails()).WillRepeatedly(ReturnRef(details));
    EXPECT_EQ("access_denied",
              termination_details_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(termination_details_format.formatValue(request_headers, response_headers,
                                                       response_trailers, stream_info, body),
                ProtoEq(ValueUtil::stringValue("access_denied")));
  }

  {
    StreamInfoFormatter bytes_sent_format("BYTES_SENT");
    EXPECT_CALL(stream_info, bytesSent()).WillRepeatedly(Return(1));
    EXPECT_EQ("1", bytes_sent_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(bytes_sent_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfo::BytesMeterSharedPtr upstream_bytes_meter{
        std::make_shared<StreamInfo::BytesMeter>()};
    upstream_bytes_meter->addWireBytesSent(1);
    StreamInfoFormatter wire_bytes_sent_format("UPSTREAM_WIRE_BYTES_SENT");
    EXPECT_CALL(stream_info, getUpstreamBytesMeter())
        .WillRepeatedly(ReturnRef(upstream_bytes_meter));
    EXPECT_EQ("1", wire_bytes_sent_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(wire_bytes_sent_format.formatValue(request_headers, response_headers,
                                                   response_trailers, stream_info, body),
                ProtoEq(ValueUtil::numberValue(1.0)));
  }

  {
    StreamInfoFormatter duration_format("DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
    EXPECT_CALL(stream_info, requestComplete()).WillRepeatedly(Return(dur));
    EXPECT_EQ("15", duration_format.format(request_headers, response_headers, response_trailers,
                                           stream_info, body));
    EXPECT_THAT(duration_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::numberValue(15.0)));
  }

  {
    StreamInfoFormatter response_flags_format("RESPONSE_FLAGS");
    ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::LocalReset))
        .WillByDefault(Return(true));
    EXPECT_EQ("LR", response_flags_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(response_flags_format.formatValue(request_headers, response_headers,
                                                  response_trailers, stream_info, body),
                ProtoEq(ValueUtil::stringValue("LR")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_LOCAL_ADDRESS");

    // Validate for IPv4 address
    auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.1.2.3", 18443)};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("127.1.2.3:18443", upstream_format.format(request_headers, response_headers,
                                                        response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.1.2.3:18443")));

    // Validate for IPv6 address
    address =
        Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 19443)};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("[::1]:19443", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("[::1]:19443")));

    // Validate for Pipe
    address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("/foo", upstream_format.format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("/foo")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT");
    auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.0.0.3", 18443)};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("127.0.0.3", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.3")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_LOCAL_PORT");

    // Validate for IPv4 address
    auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.1.2.3", 18443)};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("18443", upstream_format.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("18443")));

    // Validate for IPv6 address
    address =
        Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 19443)};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("19443", upstream_format.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("19443")));

    // Validate for Pipe
    address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
    stream_info.upstreamInfo()->setUpstreamLocalAddress(address);
    EXPECT_EQ("", upstream_format.format(request_headers, response_headers, response_trailers,
                                         stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_EQ("10.0.0.1:443", upstream_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("10.0.0.1:443")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_REMOTE_ADDRESS");
    EXPECT_EQ("10.0.0.1:443", upstream_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("10.0.0.1:443")));
  }
  {
    StreamInfoFormatter upstream_format("UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("10.0.0.1", upstream_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("10.0.0.1")));
  }
  {
    StreamInfoFormatter upstream_format("UPSTREAM_REMOTE_PORT");
    EXPECT_EQ("443", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("443")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    const std::string observable_cluster_name = "observability_name";
    auto cluster_info_mock = std::make_shared<Upstream::MockClusterInfo>();
    absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info = cluster_info_mock;
    EXPECT_CALL(stream_info, upstreamClusterInfo()).WillRepeatedly(Return(cluster_info));
    EXPECT_CALL(*cluster_info_mock, observabilityName())
        .WillRepeatedly(ReturnRef(observable_cluster_name));
    EXPECT_EQ("observability_name", upstream_format.format(request_headers, response_headers,
                                                           response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("observability_name")));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info = nullptr;
    EXPECT_CALL(stream_info, upstreamClusterInfo()).WillRepeatedly(Return(cluster_info));
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    stream_info.upstreamInfo()->setUpstreamHost(nullptr);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char*, size_t) -> Api::SysCallIntResult {
          return {-1, ENAMETOOLONG};
        }));

    StreamInfoFormatter upstream_format("HOSTNAME");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(os_sys_calls, gethostname(_, _))
        .WillOnce(Invoke([](char* name, size_t) -> Api::SysCallIntResult {
          StringUtil::strlcpy(name, "myhostname", 11);
          return {0, 0};
        }));

    StreamInfoFormatter upstream_format("HOSTNAME");
    EXPECT_EQ("myhostname", upstream_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("myhostname")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS");
    EXPECT_EQ("127.0.0.2:0", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.2:0")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.2")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_PORT");

    // Validate for IPv4 address
    auto address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("127.1.2.3", 8443)};
    stream_info.downstream_connection_info_provider_->setLocalAddress(address);
    EXPECT_EQ("8443", upstream_format.format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("8443")));

    // Validate for IPv6 address
    address =
        Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv6Instance("::1", 9443)};
    stream_info.downstream_connection_info_provider_->setLocalAddress(address);
    EXPECT_EQ("9443", upstream_format.format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("9443")));

    // Validate for Pipe
    address = Network::Address::InstanceConstSharedPtr{new Network::Address::PipeInstance("/foo")};
    stream_info.downstream_connection_info_provider_->setLocalAddress(address);
    EXPECT_EQ("", upstream_format.format(request_headers, response_headers, response_trailers,
                                         stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.1", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.1")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.1:0", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.1:0")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_PORT");
    EXPECT_EQ("0", upstream_format.format(request_headers, response_headers, response_trailers,
                                          stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("0")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.3", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.3")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_DIRECT_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.3:63443", upstream_format.format(request_headers, response_headers,
                                                        response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("127.0.0.3:63443")));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_DIRECT_REMOTE_PORT");
    EXPECT_EQ("63443", upstream_format.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("63443")));
  }

  {
    StreamInfoFormatter upstream_format("CONNECTION_ID");
    uint64_t id = 123;
    stream_info.downstream_connection_info_provider_->setConnectionID(id);
    EXPECT_EQ("123", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::numberValue(id)));
  }

  {
    StreamInfoFormatter upstream_format("STREAM_ID");

    StreamInfo::StreamIdProviderImpl id_provider("ffffffff-0012-0110-00ff-0c00400600ff");
    EXPECT_CALL(stream_info, getStreamIdProvider())
        .WillRepeatedly(Return(makeOptRef<const StreamInfo::StreamIdProvider>(id_provider)));

    EXPECT_EQ("ffffffff-0012-0110-00ff-0c00400600ff",
              upstream_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("ffffffff-0012-0110-00ff-0c00400600ff")));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name = "stub_server";
    stream_info.downstream_connection_info_provider_->setRequestedServerName(requested_server_name);
    EXPECT_EQ("stub_server", upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("stub_server")));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name;
    stream_info.downstream_connection_info_provider_->setRequestedServerName(requested_server_name);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    StreamInfoFormatter upstream_format("UPSTREAM_TRANSPORT_FAILURE_REASON");
    std::string upstream_transport_failure_reason = "SSL error";
    stream_info.upstreamInfo()->setUpstreamTransportFailureReason(
        upstream_transport_failure_reason);
    EXPECT_EQ("SSL_error", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("SSL_error")));
  }
  {
    StreamInfoFormatter upstream_format("UPSTREAM_TRANSPORT_FAILURE_REASON");
    std::string upstream_transport_failure_reason;
    stream_info.upstreamInfo()->setUpstreamTransportFailureReason(
        upstream_transport_failure_reason);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
}

TEST(SubstitutionFormatterTest, streamInfoFormatterWithSsl) {
  EXPECT_THROW(StreamInfoFormatter formatter("unknown_field"), EnvoyException);

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("VIRTUAL_CLUSTER_NAME");
    std::string virtual_cluster_name = "authN";
    stream_info.setVirtualClusterName(virtual_cluster_name);
    EXPECT_EQ("authN", upstream_format.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("VIRTUAL_CLUSTER_NAME");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
  }

  {
    // Use a local stream info for these tests as as setSslConnection can only be called once.
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san"};
    EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("san", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("san")));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san1", "san2"};
    EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("san1,san2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, uriSanPeerCertificate())
        .WillRepeatedly(Return(std::vector<std::string>()));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_URI_SAN");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san"};
    EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillRepeatedly(Return(sans));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("san", upstream_format.format(request_headers, response_headers, response_trailers,
                                            stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("san")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::vector<std::string> sans{"san1", "san2"};
    EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillRepeatedly(Return(sans));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("san1,san2", upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, uriSanLocalCertificate())
        .WillRepeatedly(Return(std::vector<std::string>()));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_URI_SAN");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_local = "subject";
    EXPECT_CALL(*connection_info, subjectLocalCertificate())
        .WillRepeatedly(ReturnRef(subject_local));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("subject", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("subject")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectLocalCertificate())
        .WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_SUBJECT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_peer = "subject";
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject_peer));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("subject", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("subject")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string session_id = "deadbeef";
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(session_id));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("deadbeef", upstream_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("deadbeef")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_SESSION_ID");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString())
        .WillRepeatedly(Return("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384",
              upstream_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString()).WillRepeatedly(Return(""));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_CIPHER");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string tlsVersion = "TLSv1.2";
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(tlsVersion));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("TLSv1.2", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("TLSv1.2")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_TLS_VERSION");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
    EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(expected_sha, upstream_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(expected_sha)));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha;
    EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_256");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_1");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha = "685a2db593d5f86d346cb1a297009c3b467ad77f1944aa799039a2fb3d531f3f";
    EXPECT_CALL(*connection_info, sha1PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(expected_sha, upstream_format.format(request_headers, response_headers,
                                                   response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(expected_sha)));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_1");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_sha;
    EXPECT_CALL(*connection_info, sha1PeerCertificateDigest())
        .WillRepeatedly(ReturnRef(expected_sha));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_FINGERPRINT_1");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string serial_number = "b8b5ecc898f2124a";
    EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
        .WillRepeatedly(ReturnRef(serial_number));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("b8b5ecc898f2124a", upstream_format.format(request_headers, response_headers,
                                                         response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("b8b5ecc898f2124a")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
        .WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SERIAL");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string issuer_peer =
        "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(issuer_peer));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
              upstream_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_ISSUER");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string subject_peer =
        "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject_peer));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
              upstream_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_SUBJECT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_cert = "<some cert>";
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(expected_cert));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(expected_cert, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(expected_cert)));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_cert = "";
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(expected_cert));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    StreamInfoFormatter upstream_format("DOWNSTREAM_PEER_CERT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_SESSION_ID");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_SESSION_ID");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string session_id = "deadbeef";
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(session_id));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ("deadbeef", upstream_format.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("deadbeef")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_SESSION_ID");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_CIPHER");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_CIPHER");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString())
        .WillRepeatedly(Return("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384"));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ("TLS_DHE_RSA_WITH_AES_256_GCM_SHA384",
              upstream_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_CIPHER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, ciphersuiteString()).WillRepeatedly(Return(""));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_VERSION");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_VERSION");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string tlsVersion = "TLSv1.2";
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(tlsVersion));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ("TLSv1.2", upstream_format.format(request_headers, response_headers,
                                                response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue("TLSv1.2")));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_TLS_VERSION");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, tlsVersion()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_ISSUER");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));

    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_ISSUER");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_ISSUER");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    const std::string issuer_peer =
        "CN=Test CA,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";
    EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillRepeatedly(ReturnRef(issuer_peer));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(issuer_peer, upstream_format.format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(issuer_peer)));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_CERT");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_CERT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_CERT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string expected_cert = "<some cert>";
    EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
        .WillRepeatedly(ReturnRef(expected_cert));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(expected_cert, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(expected_cert)));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_SUBJECT");
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_SUBJECT");
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(EMPTY_STRING));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, upstream_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    StreamInfoFormatter upstream_format("UPSTREAM_PEER_SUBJECT");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    std::string subject = "subject";
    EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(subject, upstream_format.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(upstream_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::stringValue(subject)));
  }
}

TEST(SubstitutionFormatterTest, requestHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};
  std::string body;

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, response_trailer,
                                      stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("GET")));
  }

  {
    RequestHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("/", formatter.format(request_header, response_header, response_trailer, stream_info,
                                    body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("/")));
  }

  {
    RequestHeaderFormatter formatter(":TEST", ":METHOD", absl::optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, response_trailer,
                                      stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("GET")));
  }

  {
    RequestHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>(2));
    EXPECT_EQ("GE", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("GE")));
  }
}

TEST(SubstitutionFormatterTest, headersByteSizeFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};
  std::string body;

  {
    HeadersByteSizeFormatter formatter(HeadersByteSizeFormatter::HeaderType::RequestHeaders);
    EXPECT_EQ(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        "16");
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(16)));
  }
  {
    HeadersByteSizeFormatter formatter(HeadersByteSizeFormatter::HeaderType::ResponseHeaders);
    EXPECT_EQ(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        "10");
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(10)));
  }
  {
    HeadersByteSizeFormatter formatter(HeadersByteSizeFormatter::HeaderType::ResponseTrailers);
    EXPECT_EQ(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        "23");
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(23)));
  }
}

TEST(SubstitutionFormatterTest, responseHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};
  std::string body;

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, response_trailer,
                                      stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("PUT")));
  }

  {
    ResponseHeaderFormatter formatter("test", ":method", absl::optional<size_t>());
    EXPECT_EQ("test", formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("test")));
  }

  {
    ResponseHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, response_trailer,
                                      stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("PUT")));
  }

  {
    ResponseHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>(2));
    EXPECT_EQ("PU", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("PU")));
  }
}

TEST(SubstitutionFormatterTest, responseTrailerFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};
  std::string body;

  {
    ResponseTrailerFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("POST", formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("POST")));
  }

  {
    ResponseTrailerFormatter formatter("test-2", ":method", absl::optional<size_t>());
    EXPECT_EQ("test-2", formatter.format(request_header, response_header, response_trailer,
                                         stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("test-2")));
  }

  {
    ResponseTrailerFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("POST", formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("POST")));
  }

  {
    ResponseTrailerFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }

  {
    ResponseTrailerFormatter formatter(":method", "", absl::optional<size_t>(2));
    EXPECT_EQ("PO", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("PO")));
  }
}

/**
 * Populate a metadata object with the following test data:
 * "com.test": {"test_key":"test_value","test_obj":{"inner_key":"inner_value"}}
 */
void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata) {
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  ProtobufWkt::Struct struct_inner;
  (*struct_inner.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = struct_inner;
  fields_map["test_obj"] = val;
  (*metadata.mutable_filter_metadata())["com.test"] = struct_obj;
}

TEST(SubstitutionFormatterTest, DynamicMetadataFormatter) {
  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  {
    DynamicMetadataFormatter formatter("com.test", {}, absl::optional<size_t>());
    std::string val =
        formatter.format(request_headers, response_headers, response_trailers, stream_info, body)
            .value();
    EXPECT_TRUE(val.find("\"test_key\":\"test_value\"") != std::string::npos);
    EXPECT_TRUE(val.find("\"test_obj\":{\"inner_key\":\"inner_value\"}") != std::string::npos);

    ProtobufWkt::Value expected_val;
    expected_val.mutable_struct_value()->CopyFrom(metadata.filter_metadata().at("com.test"));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(expected_val));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>());
    EXPECT_EQ("test_value", formatter.format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::stringValue("test_value")));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj"}, absl::optional<size_t>());
    EXPECT_EQ(
        "{\"inner_key\":\"inner_value\"}",
        formatter.format(request_headers, response_headers, response_trailers, stream_info, body));

    ProtobufWkt::Value expected_val;
    (*expected_val.mutable_struct_value()->mutable_fields())["inner_key"] =
        ValueUtil::stringValue("inner_value");
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(expected_val));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj", "inner_key"},
                                       absl::optional<size_t>());
    EXPECT_EQ("inner_value", formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::stringValue("inner_value")));
  }

  // not found cases
  {
    DynamicMetadataFormatter formatter("com.notfound", {}, absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"notfound"}, absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  {
    DynamicMetadataFormatter formatter("com.test", {"test_obj", "notfound"},
                                       absl::optional<size_t>());
    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  // size limit
  {
    DynamicMetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>(5));
    EXPECT_EQ("test_", formatter.format(request_headers, response_headers, response_trailers,
                                        stream_info, body));

    // N.B. Does not truncate.
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::stringValue("test_value")));
  }
}

TEST(SubstitutionFormatterTest, FilterStateFormatter) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  stream_info.filter_state_->setData("key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("key-struct",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("key-no-serialization",
                                     std::make_unique<StreamInfo::FilterState::Object>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);

  stream_info.filter_state_->setData(
      "key-serialization-error",
      std::make_unique<TestSerializedStructFilterState>(std::chrono::seconds(-281474976710656)),
      StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  {
    FilterStateFormatter formatter("key", absl::optional<size_t>(), false);

    EXPECT_EQ("\"test_value\"", formatter.format(request_headers, response_headers,
                                                 response_trailers, stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::stringValue("test_value")));
  }
  {
    FilterStateFormatter formatter("key-struct", absl::optional<size_t>(), false);

    EXPECT_EQ(
        "{\"inner_key\":\"inner_value\"}",
        formatter.format(request_headers, response_headers, response_trailers, stream_info, body));

    ProtobufWkt::Value expected;
    (*expected.mutable_struct_value()->mutable_fields())["inner_key"] =
        ValueUtil::stringValue("inner_value");

    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(expected));
  }

  // not found case
  {
    FilterStateFormatter formatter("key-not-found", absl::optional<size_t>(), false);

    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  // no serialization case
  {
    FilterStateFormatter formatter("key-no-serialization", absl::optional<size_t>(), false);

    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  // serialization error case
  {
    FilterStateFormatter formatter("key-serialization-error", absl::optional<size_t>(), false);

    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }

  // size limit
  {
    FilterStateFormatter formatter("key", absl::optional<size_t>(5), false);

    EXPECT_EQ("\"test", formatter.format(request_headers, response_headers, response_trailers,
                                         stream_info, body));

    // N.B. Does not truncate.
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::stringValue("test_value")));
  }

  // serializeAsString case
  {
    FilterStateFormatter formatter("test_key", absl::optional<size_t>(), true);

    EXPECT_EQ("test_value By PLAIN", formatter.format(request_headers, response_headers,
                                                      response_trailers, stream_info, body));
  }

  // size limit for serializeAsString
  {
    FilterStateFormatter formatter("test_key", absl::optional<size_t>(10), true);

    EXPECT_EQ("test_value", formatter.format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
  }

  // no serialization case for serializeAsString
  {
    FilterStateFormatter formatter("key-no-serialization", absl::optional<size_t>(), true);

    EXPECT_EQ(absl::nullopt, formatter.format(request_headers, response_headers, response_trailers,
                                              stream_info, body));
    EXPECT_THAT(formatter.formatValue(request_headers, response_headers, response_trailers,
                                      stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
}

TEST(SubstitutionFormatterTest, DownstreamPeerCertVStartFormatter) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  // No downstreamSslConnection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    DownstreamPeerCertVStartFormatter cert_start_formart("DOWNSTREAM_PEER_CERT_V_START(%Y/%m/%d)");
    EXPECT_EQ(absl::nullopt, cert_start_formart.format(request_headers, response_headers,
                                                       response_trailers, stream_info, body));
    EXPECT_THAT(cert_start_formart.formatValue(request_headers, response_headers, response_trailers,
                                               stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No validFromPeerCertificate
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVStartFormatter cert_start_formart("DOWNSTREAM_PEER_CERT_V_START(%Y/%m/%d)");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(absl::nullopt));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, cert_start_formart.format(request_headers, response_headers,
                                                       response_trailers, stream_info, body));
    EXPECT_THAT(cert_start_formart.formatValue(request_headers, response_headers, response_trailers,
                                               stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // Default format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVStartFormatter cert_start_format("");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              cert_start_format.format(request_headers, response_headers, response_trailers,
                                       stream_info, body));
  }
  // Custom format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVStartFormatter cert_start_format("%b %e %H:%M:%S %Y %Z");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("Mar 28 23:35:58 2018 UTC",
              cert_start_format.format(request_headers, response_headers, response_trailers,
                                       stream_info, body));
  }
}

TEST(SubstitutionFormatterTest, DownstreamPeerCertVEndFormatter) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  // No downstreamSslConnection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.downstream_connection_info_provider_->setSslConnection(nullptr);
    DownstreamPeerCertVEndFormatter cert_end_format("%Y/%m/%d");
    EXPECT_EQ(absl::nullopt, cert_end_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(cert_end_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No expirationPeerCertificate
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVEndFormatter cert_end_format("%Y/%m/%d");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, expirationPeerCertificate())
        .WillRepeatedly(Return(absl::nullopt));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, cert_end_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(cert_end_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // Default format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVEndFormatter cert_end_format("");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              cert_end_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  // Custom format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVEndFormatter cert_end_format("%b %e %H:%M:%S %Y %Z");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    EXPECT_EQ("Mar 28 23:35:58 2018 UTC",
              cert_end_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
}

TEST(SubstitutionFormatterTest, UpstreamPeerCertVStartFormatter) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  // No upstream connection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));
    UpstreamPeerCertVStartFormatter cert_start_format("%Y/%m/%d");
    EXPECT_EQ(absl::nullopt, cert_start_format.format(request_headers, response_headers,
                                                      response_trailers, stream_info, body));
    EXPECT_THAT(cert_start_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No upstreamSslConnection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    DownstreamPeerCertVStartFormatter cert_start_format("UPSTREAM_PEER_CERT_V_START(%Y/%m/%d)");
    EXPECT_EQ(absl::nullopt, cert_start_format.format(request_headers, response_headers,
                                                      response_trailers, stream_info, body));
    EXPECT_THAT(cert_start_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No validFromPeerCertificate
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    DownstreamPeerCertVStartFormatter cert_start_format("UPSTREAM_PEER_CERT_V_START(%Y/%m/%d)");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(absl::nullopt));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, cert_start_format.format(request_headers, response_headers,
                                                      response_trailers, stream_info, body));
    EXPECT_THAT(cert_start_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // Default format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    UpstreamPeerCertVStartFormatter cert_start_format("");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              cert_start_format.format(request_headers, response_headers, response_trailers,
                                       stream_info, body));
  }
  // Custom format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    UpstreamPeerCertVStartFormatter cert_start_format("%b %e %H:%M:%S %Y %Z");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ("Mar 28 23:35:58 2018 UTC",
              cert_start_format.format(request_headers, response_headers, response_trailers,
                                       stream_info, body));
  }
}

TEST(SubstitutionFormatterTest, UpstreamPeerCertVEndFormatter) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  // No upstream connection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    EXPECT_CALL(stream_info, upstreamInfo()).WillRepeatedly(Return(nullptr));
    UpstreamPeerCertVEndFormatter cert_end_format("%Y/%m/%d");
    EXPECT_EQ(absl::nullopt, cert_end_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(cert_end_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No upstreamSslConnection
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    stream_info.upstreamInfo()->setUpstreamSslConnection(nullptr);
    UpstreamPeerCertVEndFormatter cert_end_format("%Y/%m/%d");
    EXPECT_EQ(absl::nullopt, cert_end_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(cert_end_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // No expirationPeerCertificate
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    UpstreamPeerCertVEndFormatter cert_end_format("%Y/%m/%d");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    EXPECT_CALL(*connection_info, expirationPeerCertificate())
        .WillRepeatedly(Return(absl::nullopt));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(absl::nullopt, cert_end_format.format(request_headers, response_headers,
                                                    response_trailers, stream_info, body));
    EXPECT_THAT(cert_end_format.formatValue(request_headers, response_headers, response_trailers,
                                            stream_info, body),
                ProtoEq(ValueUtil::nullValue()));
  }
  // Default format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    UpstreamPeerCertVEndFormatter cert_end_format("");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              cert_end_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
  // Custom format string
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    UpstreamPeerCertVEndFormatter cert_end_format("%b %e %H:%M:%S %Y %Z");
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.upstreamInfo()->setUpstreamSslConnection(connection_info);
    EXPECT_EQ("Mar 28 23:35:58 2018 UTC",
              cert_end_format.format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
  }
}

TEST(SubstitutionFormatterTest, StartTimeFormatter) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;

  {
    StartTimeFormatter start_time_format("%Y/%m/%d");
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    EXPECT_EQ("2018/03/28", start_time_format.format(request_headers, response_headers,
                                                     response_trailers, stream_info, body));
    EXPECT_THAT(start_time_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::stringValue("2018/03/28")));
  }

  {
    StartTimeFormatter start_time_format("");
    SystemTime time;
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              start_time_format.format(request_headers, response_headers, response_trailers,
                                       stream_info, body));
    EXPECT_THAT(start_time_format.formatValue(request_headers, response_headers, response_trailers,
                                              stream_info, body),
                ProtoEq(ValueUtil::stringValue(AccessLogDateTimeFormatter::fromTime(time))));
  }
}

TEST(SubstitutionFormatterTest, GrpcStatusFormatterCamelStringTest) {
  GrpcStatusFormatter formatter("grpc-status", "", absl::optional<size_t>(),
                                GrpcStatusFormatter::Format::CamelString);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  std::vector<std::string> grpc_statuses{
      "OK",       "Canceled",       "Unknown",          "InvalidArgument",   "DeadlineExceeded",
      "NotFound", "AlreadyExists",  "PermissionDenied", "ResourceExhausted", "FailedPrecondition",
      "Aborted",  "OutOfRange",     "Unimplemented",    "Internal",          "Unavailable",
      "DataLoss", "Unauthenticated"};
  for (size_t i = 0; i < grpc_statuses.size(); ++i) {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", std::to_string(i)}};
    EXPECT_EQ(grpc_statuses[i], formatter.format(request_header, response_header, response_trailer,
                                                 stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue(grpc_statuses[i])));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"not-a-grpc-status", "13"}};
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("-1")));
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("42738")));
    response_trailer.clear();
  }
  {
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("-1")));
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("42738")));
    response_header.clear();
  }
}

TEST(SubstitutionFormatterTest, GrpcStatusFormatterSnakeStringTest) {
  GrpcStatusFormatter formatter("grpc-status", "", absl::optional<size_t>(),
                                GrpcStatusFormatter::Format::SnakeString);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  std::vector<std::string> grpc_statuses{"OK",
                                         "CANCELLED",
                                         "UNKNOWN",
                                         "INVALID_ARGUMENT",
                                         "DEADLINE_EXCEEDED",
                                         "NOT_FOUND",
                                         "ALREADY_EXISTS",
                                         "PERMISSION_DENIED",
                                         "RESOURCE_EXHAUSTED",
                                         "FAILED_PRECONDITION",
                                         "ABORTED",
                                         "OUT_OF_RANGE",
                                         "UNIMPLEMENTED",
                                         "INTERNAL",
                                         "UNAVAILABLE",
                                         "DATA_LOSS",
                                         "UNAUTHENTICATED"};
  for (size_t i = 0; i < grpc_statuses.size(); ++i) {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", std::to_string(i)}};
    EXPECT_EQ(grpc_statuses[i], formatter.format(request_header, response_header, response_trailer,
                                                 stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue(grpc_statuses[i])));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"not-a-grpc-status", "13"}};
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("-1")));
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("42738")));
    response_trailer.clear();
  }
  {
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("-1")));
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::stringValue("42738")));
    response_header.clear();
  }
}

TEST(SubstitutionFormatterTest, GrpcStatusFormatterNumberTest) {
  GrpcStatusFormatter formatter("grpc-status", "", absl::optional<size_t>(),
                                GrpcStatusFormatter::Format::Number);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  const int grpcStatuses = static_cast<int>(Grpc::Status::WellKnownGrpcStatus::MaximumKnown) + 1;

  for (size_t i = 0; i < grpcStatuses; ++i) {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", std::to_string(i)}};
    EXPECT_EQ(std::to_string(i), formatter.format(request_header, response_header, response_trailer,
                                                  stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(i)));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"not-a-grpc-status", "13"}};
    EXPECT_EQ(absl::nullopt, formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::nullValue()));
  }
  {
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(-1)));
    response_trailer = Http::TestResponseTrailerMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(42738)));
    response_trailer.clear();
  }
  {
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "-1"}};
    EXPECT_EQ("-1", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(-1)));
    response_header = Http::TestResponseHeaderMapImpl{{"grpc-status", "42738"}};
    EXPECT_EQ("42738", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
    EXPECT_THAT(
        formatter.formatValue(request_header, response_header, response_trailer, stream_info, body),
        ProtoEq(ValueUtil::numberValue(42738)));
    response_header.clear();
  }
}

void verifyStructOutput(ProtobufWkt::Struct output,
                        absl::node_hash_map<std::string, std::string> expected_map) {
  for (const auto& pair : expected_map) {
    EXPECT_EQ(output.fields().at(pair.first).string_value(), pair.second);
  }
}

TEST(SubstitutionFormatterTest, StructFormatterPlainStringTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"plain_string", "plain_string_value"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    plain_string: plain_string_value
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterPlainNumberTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  absl::node_hash_map<std::string, std::string> expected_json_map = {{"plain_number", "400"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    plain_number: 400
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterTypesTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    string_type: plain_string_value
    struct_type:
      plain_string: plain_string_value
      protocol: '%PROTOCOL%'
    list_type:
      - plain_string_value
      - '%PROTOCOL%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  const ProtobufWkt::Struct expected = TestUtility::jsonToStruct(R"EOF({
    "string_type": "plain_string_value",
    "struct_type": {
      "plain_string": "plain_string_value",
      "protocol": "HTTP/1.1"
    },
    "list_type": [
      "plain_string_value",
      "HTTP/1.1"
    ]
  })EOF");
  const ProtobufWkt::Struct out_struct =
      formatter.format(request_header, response_header, response_trailer, stream_info, body);
  EXPECT_TRUE(TestUtility::protoEqual(out_struct, expected));
}

// Test that nested values are formatted properly, including inter-type nesting.
TEST(SubstitutionFormatterTest, StructFormatterNestedObjectsTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  ProtobufWkt::Struct key_mapping;
  // For both struct and list, we test 3 nesting levels of all types (string, struct and list).
  TestUtility::loadFromYaml(R"EOF(
    struct:
      struct_string: plain_string_value
      struct_protocol: '%PROTOCOL%'
      struct_struct:
        struct_struct_string: plain_string_value
        struct_struct_protocol: '%PROTOCOL%'
        struct_struct_struct:
          struct_struct_struct_string: plain_string_value
          struct_struct_struct_protocol: '%PROTOCOL%'
        struct_struct_list:
          - struct_struct_list_string
          - '%PROTOCOL%'
      struct_list:
        - struct_list_string
        - '%PROTOCOL%'
        # struct_list_struct
        - struct_list_struct_string: plain_string_value
          struct_list_struct_protocol: '%PROTOCOL%'
        # struct_list_list
        - - struct_list_list_string
          - '%PROTOCOL%'
    list:
      - list_string
      - '%PROTOCOL%'
      # list_struct
      - list_struct_string: plain_string_value
        list_struct_protocol: '%PROTOCOL%'
        list_struct_struct:
          list_struct_struct_string: plain_string_value
          list_struct_struct_protocol: '%PROTOCOL%'
        list_struct_list:
          - list_struct_list_string
          - '%PROTOCOL%'
      # list_list
      - - list_list_string
        - '%PROTOCOL%'
        # list_list_struct
        - list_list_struct_string: plain_string_value
          list_list_struct_protocol: '%PROTOCOL%'
        # list_list_list
        - - list_list_list_string
          - '%PROTOCOL%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);
  const ProtobufWkt::Struct expected = TestUtility::jsonToStruct(R"EOF({
    "struct": {
      "struct_string": "plain_string_value",
      "struct_protocol": "HTTP/1.1",
      "struct_struct": {
        "struct_struct_string": "plain_string_value",
        "struct_struct_protocol": "HTTP/1.1",
        "struct_struct_struct": {
          "struct_struct_struct_string": "plain_string_value",
          "struct_struct_struct_protocol": "HTTP/1.1",
        },
        "struct_struct_list": [
          "struct_struct_list_string",
          "HTTP/1.1",
        ],
      },
      "struct_list": [
        "struct_list_string",
        "HTTP/1.1",
        {
          "struct_list_struct_string": "plain_string_value",
          "struct_list_struct_protocol": "HTTP/1.1",
        },
        [
          "struct_list_list_string",
          "HTTP/1.1",
        ],
      ],
    },
    "list": [
      "list_string",
      "HTTP/1.1",
      {
        "list_struct_string": "plain_string_value",
        "list_struct_protocol": "HTTP/1.1",
        "list_struct_struct": {
          "list_struct_struct_string": "plain_string_value",
          "list_struct_struct_protocol": "HTTP/1.1",
        },
        "list_struct_list": [
          "list_struct_list_string",
          "HTTP/1.1",
        ]
      },
      [
        "list_list_string",
        "HTTP/1.1",
        {
          "list_list_struct_string": "plain_string_value",
          "list_list_struct_protocol": "HTTP/1.1",
        },
        [
          "list_list_list_string",
          "HTTP/1.1",
        ],
      ],
    ],
  })EOF");
  const ProtobufWkt::Struct out_struct =
      formatter.format(request_header, response_header, response_trailer, stream_info, body);
  EXPECT_TRUE(TestUtility::protoEqual(out_struct, expected));
}

TEST(SubstitutionFormatterTest, StructFormatterSingleOperatorTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  absl::node_hash_map<std::string, std::string> expected_json_map = {{"protocol", "HTTP/1.1"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    protocol: '%PROTOCOL%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, EmptyStructFormatterTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  absl::node_hash_map<std::string, std::string> expected_json_map = {{"protocol", ""}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    protocol: ''
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterNonExistentHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{{"some_response_header", "SOME_RESPONSE_HEADER"}};
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"protocol", "HTTP/1.1"},
      {"some_request_header", "SOME_REQUEST_HEADER"},
      {"nonexistent_response_header", "-"},
      {"some_response_header", "SOME_RESPONSE_HEADER"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    protocol: '%PROTOCOL%'
    some_request_header: '%REQ(some_request_header)%'
    nonexistent_response_header: '%RESP(nonexistent_response_header)%'
    some_response_header: '%RESP(some_response_header)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterAlternateHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{
      {"request_present_header", "REQUEST_PRESENT_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{
      {"response_present_header", "RESPONSE_PRESENT_HEADER"}};
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"request_present_header_or_request_absent_header", "REQUEST_PRESENT_HEADER"},
      {"request_absent_header_or_request_present_header", "REQUEST_PRESENT_HEADER"},
      {"response_absent_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"},
      {"response_present_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    request_present_header_or_request_absent_header:
    '%REQ(request_present_header?request_absent_header)%'
    request_absent_header_or_request_present_header:
    '%REQ(request_absent_header?request_present_header)%'
    response_absent_header_or_response_absent_header:
    '%RESP(response_absent_header?response_present_header)%'
    response_present_header_or_response_absent_header:
    '%RESP(response_present_header?response_absent_header)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key", "test_value"},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "inner_value"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%DYNAMIC_METADATA(com.test:test_key)%'
    test_obj: '%DYNAMIC_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%DYNAMIC_METADATA(com.test:test_obj:inner_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterTypedDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%DYNAMIC_METADATA(com.test:test_key)%'
    test_obj: '%DYNAMIC_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%DYNAMIC_METADATA(com.test:test_obj:inner_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, true, false);

  ProtobufWkt::Struct output =
      formatter.format(request_header, response_header, response_trailer, stream_info, body);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value", fields.at("test_key").string_value());
  EXPECT_EQ("inner_value", fields.at("test_obj.inner_key").string_value());
  EXPECT_EQ("inner_value",
            fields.at("test_obj").struct_value().fields().at("inner_key").string_value());
}

TEST(SubstitutionFormatterTest, StructFormatterClusterMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>> cluster =
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(**cluster, metadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(stream_info, upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));
  EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key", "test_value"},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "inner_value"},
      {"test_obj.non_existing_key", "-"},
  };

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%CLUSTER_METADATA(com.test:test_key)%'
    test_obj: '%CLUSTER_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%CLUSTER_METADATA(com.test:test_obj:inner_key)%'
    test_obj.non_existing_key: '%CLUSTER_METADATA(com.test:test_obj:non_existing_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterTypedClusterMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>> cluster =
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(**cluster, metadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(stream_info, upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));
  EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%CLUSTER_METADATA(com.test:test_key)%'
    test_obj: '%CLUSTER_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%CLUSTER_METADATA(com.test:test_obj:inner_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, true, false);

  ProtobufWkt::Struct output =
      formatter.format(request_header, response_header, response_trailer, stream_info, body);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value", fields.at("test_key").string_value());
  EXPECT_EQ("inner_value", fields.at("test_obj.inner_key").string_value());
  EXPECT_EQ("inner_value",
            fields.at("test_obj").struct_value().fields().at("inner_key").string_value());
}

TEST(SubstitutionFormatterTest, StructFormatterClusterMetadataNoClusterInfoTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  absl::node_hash_map<std::string, std::string> expected_json_map = {{"test_key", "-"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%CLUSTER_METADATA(com.test:test_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  // Empty optional (absl::nullopt)
  {
    EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillOnce(Return(absl::nullopt));
    verifyStructOutput(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        expected_json_map);
  }
  // Empty cluster info (nullptr)
  {
    EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillOnce(Return(nullptr));
    verifyStructOutput(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        expected_json_map);
  }
}

TEST(SubstitutionFormatterTest, StructFormatterUpstreamHostMetadataTest) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  const auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  populateMetadataTestData(*metadata);
  // Get pointers to MockUpstreamInfo  and MockHostDescription.
  std::shared_ptr<StreamInfo::MockUpstreamInfo> mock_upstream_info =
      std::dynamic_pointer_cast<StreamInfo::MockUpstreamInfo>(stream_info.upstreamInfo());
  std::shared_ptr<const Upstream::MockHostDescription> mock_host_description =
      std::dynamic_pointer_cast<const Upstream::MockHostDescription>(
          mock_upstream_info->upstreamHost());
  EXPECT_CALL(*mock_host_description, metadata()).WillRepeatedly(Return(metadata));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key", "test_value"},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "inner_value"},
      {"test_obj.non_existing_key", "-"},
  };

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%UPSTREAM_METADATA(com.test:test_key)%'
    test_obj: '%UPSTREAM_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%UPSTREAM_METADATA(com.test:test_obj:inner_key)%'
    test_obj.non_existing_key: '%UPSTREAM_METADATA(com.test:test_obj:non_existing_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterUpstreamHostMetadataNullPtrs) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  absl::node_hash_map<std::string, std::string> expected_json_map = {{"test_key", "-"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%UPSTREAM_METADATA(com.test:test_key)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  // Empty optional (absl::nullopt)
  {
    EXPECT_CALL(Const(stream_info), upstreamInfo()).WillOnce(Return(absl::nullopt));
    verifyStructOutput(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        expected_json_map);
    testing::Mock::VerifyAndClearExpectations(&stream_info);
  }
  // Empty host description info (nullptr)
  {
    std::shared_ptr<StreamInfo::MockUpstreamInfo> mock_upstream_info =
        std::dynamic_pointer_cast<StreamInfo::MockUpstreamInfo>(stream_info.upstreamInfo());
    EXPECT_CALL(*mock_upstream_info, upstreamHost()).WillOnce(Return(nullptr));
    verifyStructOutput(
        formatter.format(request_header, response_header, response_trailer, stream_info, body),
        expected_json_map);
  }
}

TEST(SubstitutionFormatterTest, StructFormatterFilterStateTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData("test_key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key", "\"test_value\""}, {"test_obj", "{\"inner_key\":\"inner_value\"}"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%FILTER_STATE(test_key)%'
    test_obj: '%FILTER_STATE(test_obj)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterOmitEmptyTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).Times(testing::AtLeast(1));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
      test_key_filter_state: '%FILTER_STATE(nonexistent_key)%'
      test_key_req: '%REQ(nonexistent_key)%'
      test_key_res: '%RESP(nonexistent_key)%'
      test_key_dynamic_metadata: '%DYNAMIC_METADATA(nonexistent_key)%'
    )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, true);

  verifyStructOutput(
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body),
      {});
}

TEST(SubstitutionFormatterTest, StructFormatterTypedFilterStateTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData("test_key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%FILTER_STATE(test_key)%'
    test_obj: '%FILTER_STATE(test_obj)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, true, false);

  ProtobufWkt::Struct output =
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value", fields.at("test_key").string_value());
  EXPECT_EQ("inner_value",
            fields.at("test_obj").struct_value().fields().at("inner_key").string_value());
}

// Test new specifier (PLAIN/TYPED) of FilterState. Ensure that after adding additional specifier,
// the FilterState can call the serializeAsProto or serializeAsString methods correctly.
TEST(SubstitutionFormatterTest, FilterStateSpeciferTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key_plain", "test_value By PLAIN"},
      {"test_key_typed", "\"test_value By TYPED\""},
  };

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key_plain: '%FILTER_STATE(test_key:PLAIN)%'
    test_key_typed: '%FILTER_STATE(test_key:TYPED)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body),
      expected_json_map);
}

// Test new specifier (PLAIN/TYPED) of FilterState and convert the output log string to proto
// and then verify the result.
TEST(SubstitutionFormatterTest, TypedFilterStateSpeciferTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key_plain: '%FILTER_STATE(test_key:PLAIN)%'
    test_key_typed: '%FILTER_STATE(test_key:TYPED)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, true, false);

  ProtobufWkt::Struct output =
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body);

  const auto& fields = output.fields();
  EXPECT_EQ("test_value By PLAIN", fields.at("test_key_plain").string_value());
  EXPECT_EQ("test_value By TYPED", fields.at("test_key_typed").string_value());
}

// Error specifier will cause an exception to be thrown.
TEST(SubstitutionFormatterTest, FilterStateErrorSpeciferTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);

  // 'ABCDE' is error specifier.
  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key_plain: '%FILTER_STATE(test_key:ABCDE)%'
    test_key_typed: '%FILTER_STATE(test_key:TYPED)%'
  )EOF",
                            key_mapping);
  EXPECT_THROW_WITH_MESSAGE(StructFormatter formatter(key_mapping, false, false), EnvoyException,
                            "Invalid filter state serialize type, only support PLAIN/TYPED.");
}

TEST(SubstitutionFormatterTest, StructFormatterStartTimeTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  time_t expected_time_in_epoch = 1522280158;
  SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"simple_date", "2018/03/28"},
      {"test_time", fmt::format("{}", expected_time_in_epoch)},
      {"bad_format", "bad_format"},
      {"default", "2018-03-28T23:35:58.000Z"},
      {"all_zeroes", "000000000.0.00.000"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    simple_date: '%START_TIME(%Y/%m/%d)%'
    test_time: '%START_TIME(%s)%'
    bad_format: '%START_TIME(bad_format)%'
    default: '%START_TIME%'
    all_zeroes: '%START_TIME(%f.%1f.%2f.%3f)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, false, false);

  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);
}

TEST(SubstitutionFormatterTest, StructFormatterMultiTokenTest) {
  {
    StreamInfo::MockStreamInfo stream_info;
    Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
    Http::TestResponseHeaderMapImpl response_header{
        {"some_response_header", "SOME_RESPONSE_HEADER"}};
    Http::TestResponseTrailerMapImpl response_trailer;
    std::string body;

    absl::node_hash_map<std::string, std::string> expected_json_map = {
        {"multi_token_field", "HTTP/1.1 plainstring SOME_REQUEST_HEADER SOME_RESPONSE_HEADER"}};

    ProtobufWkt::Struct key_mapping;
    TestUtility::loadFromYaml(R"EOF(
      multi_token_field: '%PROTOCOL% plainstring %REQ(some_request_header)%
      %RESP(some_response_header)%'
    )EOF",
                              key_mapping);
    for (const bool preserve_types : {false, true}) {
      StructFormatter formatter(key_mapping, preserve_types, false);

      absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
      EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

      verifyStructOutput(
          formatter.format(request_header, response_header, response_trailer, stream_info, body),
          expected_json_map);
    }
  }
}

TEST(SubstitutionFormatterTest, StructFormatterTypedTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  std::string body;
  MockTimeSystem time_system;
  EXPECT_CALL(time_system, monotonicTime)
      .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(5000000))));
  stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);

  ProtobufWkt::Value list;
  list.mutable_list_value()->add_values()->set_bool_value(true);
  list.mutable_list_value()->add_values()->set_string_value("two");
  list.mutable_list_value()->add_values()->set_number_value(3.14);

  ProtobufWkt::Struct s;
  (*s.mutable_fields())["list"] = list;

  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(s),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    request_duration: '%REQUEST_DURATION%'
    request_duration_multi: '%REQUEST_DURATION%ms'
    filter_state: '%FILTER_STATE(test_obj)%'
  )EOF",
                            key_mapping);
  StructFormatter formatter(key_mapping, true, false);

  ProtobufWkt::Struct output =
      formatter.format(request_headers, response_headers, response_trailers, stream_info, body);

  EXPECT_THAT(output.fields().at("request_duration"), ProtoEq(ValueUtil::numberValue(5.0)));
  EXPECT_THAT(output.fields().at("request_duration_multi"), ProtoEq(ValueUtil::stringValue("5ms")));

  ProtobufWkt::Value expected;
  expected.mutable_struct_value()->CopyFrom(s);
  EXPECT_THAT(output.fields().at("filter_state"), ProtoEq(expected));
}

TEST(SubstitutionFormatterTest, JsonFormatterTest) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));
  MockTimeSystem time_system;
  EXPECT_CALL(time_system, monotonicTime)
      .WillOnce(Return(MonotonicTime(std::chrono::nanoseconds(5000000))));
  stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    request_duration: '%REQUEST_DURATION%'
    nested_level:
      plain_string: plain_string_value
      protocol: '%PROTOCOL%'
  )EOF",
                            key_mapping);
  JsonFormatterImpl formatter(key_mapping, false, false);

  const std::string expected = R"EOF({
    "request_duration": "5",
    "nested_level": {
      "plain_string": "plain_string_value",
      "protocol": "HTTP/1.1"
    }
  })EOF";

  const std::string out_json =
      formatter.format(request_header, response_header, response_trailer, stream_info, body);
  EXPECT_TRUE(TestUtility::jsonStringEqual(out_json, expected));
}

TEST(SubstitutionFormatterTest, CompositeFormatterSuccess) {
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const std::string format = "{{%PROTOCOL%}}   %RESP(not exist)%++%RESP(test)% "
                               "%REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)%"
                               "\t@%TRAILER(THIRD)%@\t%TRAILER(TEST?TEST-2)%[]";
    FormatterImpl formatter(format, false);

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

    EXPECT_EQ(
        "{{HTTP/1.1}}   -++test GET PUT\t@POST@\ttest-2[]",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const std::string format = "{}*JUST PLAIN string]";
    FormatterImpl formatter(format, false);

    EXPECT_EQ(format, formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const std::string format = "%REQ(first):3%|%REQ(first):1%|%RESP(first?second):2%|%REQ(first):"
                               "10%|%TRAILER(second?third):3%";

    FormatterImpl formatter(format, false);

    EXPECT_EQ("GET|G|PU|GET|POS", formatter.format(request_header, response_header,
                                                   response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    envoy::config::core::v3::Metadata metadata;
    populateMetadataTestData(metadata);
    EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    const std::string format = "%DYNAMIC_METADATA(com.test:test_key)%|%DYNAMIC_METADATA(com.test:"
                               "test_obj)%|%DYNAMIC_METADATA(com.test:test_obj:inner_key)%";
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        "test_value|{\"inner_key\":\"inner_value\"}|inner_value",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));
    stream_info.filter_state_->setData("testing",
                                       std::make_unique<Router::StringAccessorImpl>("test_value"),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    stream_info.filter_state_->setData("serialized",
                                       std::make_unique<TestSerializedUnknownFilterState>(),
                                       StreamInfo::FilterState::StateType::ReadOnly,
                                       StreamInfo::FilterState::LifeSpan::FilterChain);
    const std::string format = "%FILTER_STATE(testing)%|%FILTER_STATE(serialized)%|"
                               "%FILTER_STATE(testing):8%|%FILTER_STATE(nonexisting)%";
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        "\"test_value\"|-|\"test_va|-",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // Various START_TIME formats
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    time_t expected_time_in_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        fmt::format("2018/03/28|{}|bad_format|2018-03-28T23:35:58.000Z|000000000.0.00.000",
                    expected_time_in_epoch),
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // Various DOWNSTREAM_PEER_CERT_V_START formats (similar to START_TIME)
    const std::string format =
        "%DOWNSTREAM_PEER_CERT_V_START(%Y/%m/"
        "%d)%|%DOWNSTREAM_PEER_CERT_V_START(%s)%|%DOWNSTREAM_PEER_CERT_V_START(bad_format)%|"
        "%DOWNSTREAM_PEER_CERT_V_START%|%DOWNSTREAM_PEER_CERT_V_START(%f.%1f.%2f.%3f)%";

    time_t expected_time_in_epoch = 1522280158;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
    EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        fmt::format("2018/03/28|{}|bad_format|2018-03-28T23:35:58.000Z|000000000.0.00.000",
                    expected_time_in_epoch),
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // Various DOWNSTREAM_PEER_CERT_V_END formats (similar to START_TIME)
    const std::string format =
        "%DOWNSTREAM_PEER_CERT_V_END(%Y/%m/"
        "%d)%|%DOWNSTREAM_PEER_CERT_V_END(%s)%|%DOWNSTREAM_PEER_CERT_V_END(bad_format)%|"
        "%DOWNSTREAM_PEER_CERT_V_END%|%DOWNSTREAM_PEER_CERT_V_END(%f.%1f.%2f.%3f)%";

    time_t expected_time_in_epoch = 1522280158;
    auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
    SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
    EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(time));
    stream_info.downstream_connection_info_provider_->setSslConnection(connection_info);
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        fmt::format("2018/03/28|{}|bad_format|2018-03-28T23:35:58.000Z|000000000.0.00.000",
                    expected_time_in_epoch),
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // This tests the beginning of time.
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    const time_t test_epoch = 0;
    const SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format, false);

    EXPECT_EQ(
        "1970/01/01|0|bad_format|1970-01-01T00:00:00.000Z|000000000.0.00.000",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // This tests multiple START_TIMEs.
    const std::string format =
        "%START_TIME(%s.%3f)%|%START_TIME(%s.%4f)%|%START_TIME(%s.%5f)%|%START_TIME(%s.%6f)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format, false);
    EXPECT_EQ(
        "1522796769.123|1522796769.1234|1522796769.12345|1522796769.123456",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const std::string format =
        "%START_TIME(segment1:%s.%3f|segment2:%s.%4f|seg3:%s.%6f|%s-%3f-asdf-%9f|.%7f:segm5:%Y)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format, false);
    EXPECT_EQ(
        "segment1:1522796769.123|segment2:1522796769.1234|seg3:1522796769.123456|1522796769-"
        "123-asdf-123456000|.1234560:segm5:2018",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    // This tests START_TIME specifier that has shorter segments when formatted, i.e.
    // absl::FormatTime("%%%%"") equals "%%", %1f will have 1 as its size.
    const std::string format = "%START_TIME(%%%%|%%%%%f|%s%%%%%3f|%1f%%%%%s)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(start_time));
    FormatterImpl formatter(format, false);
    EXPECT_EQ(
        "%%|%%123456000|1522796769%%123|1%%1522796769",
        formatter.format(request_header, response_header, response_trailer, stream_info, body));
  }

  // The %E formatting option in Absl::FormatTime() behaves differently for non Linux platforms.
  //
  // See:
  // https://github.com/abseil/abseil-cpp/issues/869
  // https://github.com/google/cctz/issues/180
#if !defined(WIN32) && !defined(__APPLE__)
  {
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const std::string format = "%START_TIME(%E4n)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("%E4n", formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
  }
#endif
}

TEST(SubstitutionFormatterTest, CompositeFormatterEmpty) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{};
  Http::TestResponseHeaderMapImpl response_header{};
  Http::TestResponseTrailerMapImpl response_trailer{};
  std::string body;

  {
    const std::string format = "%PROTOCOL%|%RESP(not exist)%|"
                               "%REQ(FIRST?SECOND)%|%RESP(FIRST?SECOND)%|"
                               "%TRAILER(THIRD)%|%TRAILER(TEST?TEST-2)%";
    FormatterImpl formatter(format, false);

    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(absl::nullopt));

    EXPECT_EQ("-|-|-|-|-|-", formatter.format(request_header, response_header, response_trailer,
                                              stream_info, body));
  }

  {
    const std::string format = "%PROTOCOL%|%RESP(not exist)%|"
                               "%REQ(FIRST?SECOND)%%RESP(FIRST?SECOND)%|"
                               "%TRAILER(THIRD)%|%TRAILER(TEST?TEST-2)%";
    FormatterImpl formatter(format, true);

    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(absl::nullopt));

    EXPECT_EQ("||||", formatter.format(request_header, response_header, response_trailer,
                                       stream_info, body));
  }

  {
    envoy::config::core::v3::Metadata metadata;
    EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    const std::string format = "%DYNAMIC_METADATA(com.test:test_key)%|%DYNAMIC_METADATA(com.test:"
                               "test_obj)%|%DYNAMIC_METADATA(com.test:test_obj:inner_key)%";
    FormatterImpl formatter(format, false);

    EXPECT_EQ("-|-|-", formatter.format(request_header, response_header, response_trailer,
                                        stream_info, body));
  }

  {
    envoy::config::core::v3::Metadata metadata;
    EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    const std::string format = "%DYNAMIC_METADATA(com.test:test_key)%|%DYNAMIC_METADATA(com.test:"
                               "test_obj)%|%DYNAMIC_METADATA(com.test:test_obj:inner_key)%";
    FormatterImpl formatter(format, true);

    EXPECT_EQ("||", formatter.format(request_header, response_header, response_trailer, stream_info,
                                     body));
  }

  {
    EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));
    const std::string format = "%FILTER_STATE(testing)%|%FILTER_STATE(serialized)%|"
                               "%FILTER_STATE(testing):8%|%FILTER_STATE(nonexisting)%";
    FormatterImpl formatter(format, false);

    EXPECT_EQ("-|-|-|-", formatter.format(request_header, response_header, response_trailer,
                                          stream_info, body));
  }

  {
    EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));
    const std::string format = "%FILTER_STATE(testing)%|%FILTER_STATE(serialized)%|"
                               "%FILTER_STATE(testing):8%|%FILTER_STATE(nonexisting)%";
    FormatterImpl formatter(format, true);

    EXPECT_EQ("|||", formatter.format(request_header, response_header, response_trailer,
                                      stream_info, body));
  }
}

TEST(SubstitutionFormatterTest, ParserFailures) {
  SubstitutionFormatParser parser;

  std::vector<std::string> test_cases = {
      "{{%PROTOCOL%}}   ++ %REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)",
      "%PROTOCOL(should_not_have_params)%",
      "%PROTOCOL(should_not_have_params_or_length):12%",
      "%PROTOCOL:12%",
      "%REQ(FIRST?SECOND)T%",
      "RESP(FIRST)%",
      "%REQ(valid)% %NOT_VALID%",
      "%REQ(FIRST?SECOND%",
      "%HOSTNAME%PROTOCOL%",
      "%protocol%",
      "%REQ(TEST):%",
      "%REQ(TEST):3q4%",
      "%REQ(TEST):-3%",
      "%REQ(\n)%",
      "%REQ(?\n)%",
      "%RESP(TEST):%",
      "%RESP(X?Y):%",
      "%RESP(X?Y):343o24%",
      "%REQ(TEST):10",
      "REQ(:TEST):10%",
      "%REQ(TEST:10%",
      "%REQ(",
      "%REQ(X?Y?Z)%",
      "%TRAILER(TEST):%",
      "%TRAILER(TEST):23u1%",
      "%TRAILER(X?Y?Z)%",
      "%TRAILER(:TEST):10",
      "%DYNAMIC_METADATA(TEST",
      "%FILTER_STATE(TEST",
      "%FILTER_STATE()%",
      "%START_TIME(%85n)%",
      "%START_TIME(%#__88n)%",
      "%START_TIME(%En%)%",
      "%START_TIME(%4En%)%",
      "%START_TIME(%On%)%",
      "%START_TIME(%4On%)%"};

  for (const std::string& test_case : test_cases) {
    EXPECT_THROW(parser.parse(test_case), EnvoyException) << test_case;
  }
}

TEST(SubstitutionFormatterTest, ParserSuccesses) {
  SubstitutionFormatParser parser;

  std::vector<std::string> test_cases = {"%START_TIME(%E4n%)%", "%START_TIME(%O4n%)%",
                                         "%DOWNSTREAM_PEER_FINGERPRINT_256%"};

  for (const std::string& test_case : test_cases) {
    EXPECT_NO_THROW(parser.parse(test_case));
  }
}

TEST(SubstitutionFormatterTest, EmptyFormatParse) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  auto providers = SubstitutionFormatParser::parse("");

  EXPECT_EQ(providers.size(), 1);
  EXPECT_EQ("", providers[0]->format(request_headers, response_headers, response_trailers,
                                     stream_info, body));
}

TEST(SubstitutionFormatterTest, EscapingFormatParse) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  auto providers = SubstitutionFormatParser::parse("%%");

  ASSERT_EQ(providers.size(), 1);
  EXPECT_EQ("%", providers[0]->format(request_headers, response_headers, response_trailers,
                                      stream_info, body));
}

TEST(SubstitutionFormatterTest, FormatterExtension) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  std::vector<CommandParserPtr> commands;
  commands.push_back(std::make_unique<TestCommandParser>());

  auto providers = SubstitutionFormatParser::parse("foo %COMMAND_EXTENSION(x)%", commands);

  EXPECT_EQ(providers.size(), 2);
  EXPECT_EQ("TestFormatter", providers[1]->format(request_headers, response_headers,
                                                  response_trailers, stream_info, body));
}

TEST(SubstitutionFormatterTest, PercentEscapingEdgeCase) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, gethostname(_, _))
      .WillOnce(Invoke([](char* name, size_t) -> Api::SysCallIntResult {
        StringUtil::strlcpy(name, "myhostname", 11);
        return {0, 0};
      }));
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  auto providers = SubstitutionFormatParser::parse("%HOSTNAME%%PROTOCOL%");

  ASSERT_EQ(providers.size(), 2);
  EXPECT_EQ("myhostname", providers[0]->format(request_headers, response_headers, response_trailers,
                                               stream_info, body));
  EXPECT_EQ("HTTP/1.1", providers[1]->format(request_headers, response_headers, response_trailers,
                                             stream_info, body));
}

TEST(SubstitutionFormatterTest, EnvironmentFormatterTest) {
  {
    EXPECT_THROW_WITH_MESSAGE(SubstitutionFormatParser::parse("%ENVIRONMENT()%"), EnvoyException,
                              "ENVIRONMENT requires parameters");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    StreamInfo::MockStreamInfo stream_info;
    std::string body;

    auto providers = SubstitutionFormatParser::parse("%ENVIRONMENT(ENVOY_TEST_ENV)%");

    ASSERT_EQ(providers.size(), 1);

    EXPECT_EQ("-", providers[0]->format(request_headers, response_headers, response_trailers,
                                        stream_info, body));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    StreamInfo::MockStreamInfo stream_info;
    std::string body;

    TestEnvironment::setEnvVar("ENVOY_TEST_ENV", "test", 1);
    Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ENVOY_TEST_ENV"); });

    auto providers = SubstitutionFormatParser::parse("%ENVIRONMENT(ENVOY_TEST_ENV)%");

    ASSERT_EQ(providers.size(), 1);

    EXPECT_EQ("test", providers[0]->format(request_headers, response_headers, response_trailers,
                                           stream_info, body));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::TestResponseTrailerMapImpl response_trailers;
    StreamInfo::MockStreamInfo stream_info;
    std::string body;

    TestEnvironment::setEnvVar("ENVOY_TEST_ENV", "test", 1);
    Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ENVOY_TEST_ENV"); });

    auto providers = SubstitutionFormatParser::parse("%ENVIRONMENT(ENVOY_TEST_ENV):2%");

    ASSERT_EQ(providers.size(), 1);

    EXPECT_EQ("te", providers[0]->format(request_headers, response_headers, response_trailers,
                                         stream_info, body));
  }
}

TEST(SubstitutionFormatParser, SyntaxVerifierFail) {
  std::vector<
      std::tuple<CommandSyntaxChecker::CommandSyntaxFlags, std::string, absl::optional<size_t>>>
      test_cases = {
          // COMMAND_ONLY. Any additional params are not allowed
          {CommandSyntaxChecker::COMMAND_ONLY, "", 3},
          {CommandSyntaxChecker::COMMAND_ONLY, "PARAMS", 3},
          {CommandSyntaxChecker::COMMAND_ONLY, "PARAMS", absl::nullopt},
          // PARAMS_REQUIRED. Length not allowed.
          {CommandSyntaxChecker::PARAMS_REQUIRED, "", 3},
          {CommandSyntaxChecker::PARAMS_REQUIRED, "PARAM", 3},
          {CommandSyntaxChecker::PARAMS_REQUIRED, "", absl::nullopt},
          // PARAMS_REQUIRED and LENGTH_ALLOWED
          {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED, "", 3},
          {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED, "",
           absl::nullopt},
      };

  for (const auto& test_case : test_cases) {
    EXPECT_THROW(CommandSyntaxChecker::verifySyntax(std::get<0>(test_case), "TEST_TOKEN",
                                                    std::get<1>(test_case), std::get<2>(test_case)),
                 EnvoyException);
  }
}

TEST(SubstitutionFormatParser, SyntaxVerifierPass) {
  std::vector<
      std::tuple<CommandSyntaxChecker::CommandSyntaxFlags, std::string, absl::optional<size_t>>>
      test_cases = {
          // COMMAND_ONLY. Any additional params are not allowed
          {CommandSyntaxChecker::COMMAND_ONLY, "", absl::nullopt},
          // PARAMS_REQUIRED
          {CommandSyntaxChecker::PARAMS_REQUIRED, "PARAMS", absl::nullopt},
          // PARAMS_REQUIRED and LENGTH_ALLOWED
          {CommandSyntaxChecker::PARAMS_REQUIRED | CommandSyntaxChecker::LENGTH_ALLOWED, "PARAMS",
           3},
          {CommandSyntaxChecker::PARAMS_REQUIRED, "PARAMS", absl::nullopt},
          // ALLOWS_PARAMS
          {CommandSyntaxChecker::PARAMS_OPTIONAL, "PARAMS", absl::nullopt},
          {CommandSyntaxChecker::PARAMS_OPTIONAL, "", absl::nullopt},
          // ALLOWS_PARAMS and LENGTH_ALLOWED
          {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED, "PARAMS",
           3},
          {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED, "", 3},
          {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED, "PARAMS",
           absl::nullopt},
          {CommandSyntaxChecker::PARAMS_OPTIONAL | CommandSyntaxChecker::LENGTH_ALLOWED, "",
           absl::nullopt}};

  for (const auto& test_case : test_cases) {
    EXPECT_NO_THROW(CommandSyntaxChecker::verifySyntax(
        std::get<0>(test_case), "TEST_TOKEN", std::get<1>(test_case), std::get<2>(test_case)));
  }
}
} // namespace
} // namespace Formatter
} // namespace Envoy
