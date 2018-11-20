#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/access_log/access_log_formatter.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace AccessLog {

TEST(AccessLogFormatUtilsTest, protocolToString) {
  EXPECT_EQ("HTTP/1.0", AccessLogFormatUtils::protocolToString(Http::Protocol::Http10));
  EXPECT_EQ("HTTP/1.1", AccessLogFormatUtils::protocolToString(Http::Protocol::Http11));
  EXPECT_EQ("HTTP/2", AccessLogFormatUtils::protocolToString(Http::Protocol::Http2));
  EXPECT_EQ("-", AccessLogFormatUtils::protocolToString({}));
}

TEST(AccessLogFormatterTest, plainStringFormatter) {
  PlainStringFormatter formatter("plain");
  Http::TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};
  StreamInfo::MockStreamInfo stream_info;

  EXPECT_EQ("plain", formatter.format(header, header, header, stream_info));
}

TEST(AccessLogFormatterTest, streamInfoFormatter) {
  EXPECT_THROW(StreamInfoFormatter formatter("unknown_field"), EnvoyException);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(5000000);
    EXPECT_CALL(stream_info, lastDownstreamRxByteReceived()).WillOnce(Return(dur));
    EXPECT_EQ("5", request_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(stream_info, lastDownstreamRxByteReceived()).WillOnce(Return(dur));
    EXPECT_EQ("-", request_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(10000000);
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("10", response_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("-", response_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    absl::optional<std::chrono::nanoseconds> dur_upstream = std::chrono::nanoseconds(10000000);
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur_upstream));
    absl::optional<std::chrono::nanoseconds> dur_downstream = std::chrono::nanoseconds(25000000);
    EXPECT_CALL(stream_info, lastDownstreamTxByteSent()).WillRepeatedly(Return(dur_downstream));

    EXPECT_EQ("15", ttlb_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter ttlb_duration_format("RESPONSE_TX_DURATION");

    absl::optional<std::chrono::nanoseconds> dur_upstream;
    EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur_upstream));
    absl::optional<std::chrono::nanoseconds> dur_downstream;
    EXPECT_CALL(stream_info, lastDownstreamTxByteSent()).WillRepeatedly(Return(dur_downstream));

    EXPECT_EQ("-", ttlb_duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter bytes_received_format("BYTES_RECEIVED");
    EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_received_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter protocol_format("PROTOCOL");
    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillOnce(Return(protocol));
    EXPECT_EQ("HTTP/1.1", protocol_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter response_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("200", response_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter response_code_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code;
    EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("0", response_code_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter bytes_sent_format("BYTES_SENT");
    EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_sent_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter duration_format("DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
    EXPECT_CALL(stream_info, requestComplete()).WillRepeatedly(Return(dur));
    EXPECT_EQ("15", duration_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter response_flags_format("RESPONSE_FLAGS");
    ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::LocalReset))
        .WillByDefault(Return(true));
    EXPECT_EQ("LR", response_flags_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_EQ("10.0.0.1:443", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    const std::string upstream_cluster_name = "cluster_name";
    EXPECT_CALL(stream_info.host_->cluster_, name()).WillOnce(ReturnRef(upstream_cluster_name));
    EXPECT_EQ("cluster_name", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS");
    EXPECT_EQ("127.0.0.2:0", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.2", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.1", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.1:0", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name = "stub_server";
    EXPECT_CALL(stream_info, requestedServerName())
        .WillRepeatedly(ReturnRef(requested_server_name));
    EXPECT_EQ("stub_server", upstream_format.format(header, header, header, stream_info));
  }

  {
    StreamInfoFormatter upstream_format("REQUESTED_SERVER_NAME");
    std::string requested_server_name;
    EXPECT_CALL(stream_info, requestedServerName())
        .WillRepeatedly(ReturnRef(requested_server_name));
    EXPECT_EQ("-", upstream_format.format(header, header, header, stream_info));
  }
}

TEST(AccessLogFormatterTest, requestHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{":method", "PUT"}};
  Http::TestHeaderMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>());
    EXPECT_EQ("GET",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    RequestHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("/",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    RequestHeaderFormatter formatter(":TEST", ":METHOD", absl::optional<size_t>());
    EXPECT_EQ("GET",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    RequestHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }
}

TEST(AccessLogFormatterTest, responseHeaderFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestHeaderMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("PUT",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseHeaderFormatter formatter("test", ":method", absl::optional<size_t>());
    EXPECT_EQ("test",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("PUT",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }
}

TEST(AccessLogFormatterTest, responseTrailerFormatter) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};
  Http::TestHeaderMapImpl response_trailer{{":method", "POST"}, {"test-2", "test-2"}};

  {
    ResponseTrailerFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("POST",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseTrailerFormatter formatter("test-2", ":method", absl::optional<size_t>());
    EXPECT_EQ("test-2",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseTrailerFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("POST",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    ResponseTrailerFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }
}

/**
 * Populate a metadata object with the following test data:
 * "com.test": {"test_key":"test_value","test_obj":{"inner_key":"inner_value"}}
 */
void populateMetadataTestData(envoy::api::v2::core::Metadata& metadata) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  auto& fields_map = *struct_obj.mutable_fields();
  val.set_string_value("test_value");
  fields_map["test_key"] = val;
  val.set_string_value("inner_value");
  ProtobufWkt::Struct struct_inner;
  (*struct_inner.mutable_fields())["inner_key"] = val;
  val.clear_string_value();
  *val.mutable_struct_value() = struct_inner;
  fields_map["test_obj"] = val;
  (*metadata.mutable_filter_metadata())["com.test"] = struct_obj;
}

TEST(AccessLogFormatterTest, dynamicMetadataFormatter) {
  envoy::api::v2::core::Metadata metadata;
  populateMetadataTestData(metadata);

  {
    MetadataFormatter formatter("com.test", {}, absl::optional<size_t>());
    std::string json = formatter.format(metadata);
    EXPECT_TRUE(json.find("\"test_key\":\"test_value\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"test_obj\":{\"inner_key\":\"inner_value\"}") != std::string::npos);
  }
  {
    MetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>());
    std::string json = formatter.format(metadata);
    EXPECT_EQ("\"test_value\"", json);
  }
  {
    MetadataFormatter formatter("com.test", {"test_obj"}, absl::optional<size_t>());
    std::string json = formatter.format(metadata);
    EXPECT_EQ("{\"inner_key\":\"inner_value\"}", json);
  }
  {
    MetadataFormatter formatter("com.test", {"test_obj", "inner_key"}, absl::optional<size_t>());
    std::string json = formatter.format(metadata);
    EXPECT_EQ("\"inner_value\"", json);
  }
  // not found cases
  {
    MetadataFormatter formatter("com.notfound", {}, absl::optional<size_t>());
    EXPECT_EQ("-", formatter.format(metadata));
  }
  {
    MetadataFormatter formatter("com.test", {"notfound"}, absl::optional<size_t>());
    EXPECT_EQ("-", formatter.format(metadata));
  }
  {
    MetadataFormatter formatter("com.test", {"test_obj", "notfound"}, absl::optional<size_t>());
    EXPECT_EQ("-", formatter.format(metadata));
  }
  // size limit
  {
    MetadataFormatter formatter("com.test", {"test_key"}, absl::optional<size_t>(5));
    std::string json = formatter.format(metadata);
    EXPECT_EQ("\"test", json);
  }
}

TEST(AccessLogFormatterTest, startTimeFormatter) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};

  {
    StartTimeFormatter start_time_format("%Y/%m/%d");
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(time));
    EXPECT_EQ("2018/03/28", start_time_format.format(header, header, header, stream_info));
  }

  {
    StartTimeFormatter start_time_format("");
    SystemTime time;
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(time));
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              start_time_format.format(header, header, header, stream_info));
  }
}

void verifyJsonOutput(std::string json_string,
                      std::unordered_map<std::string, std::string> expected_map) {
  const auto parsed = Json::Factory::loadFromString(json_string);

  // Every json log line should have only one newline character, and it should be the last character
  // in the string
  const auto newline_pos = json_string.find('\n');
  EXPECT_NE(newline_pos, std::string::npos);
  EXPECT_EQ(newline_pos, json_string.length() - 1);

  for (const auto& pair : expected_map) {
    EXPECT_EQ(parsed->getString(pair.first), pair.second);
  }
}

TEST(AccessLogFormatterTest, JsonFormatterPlainStringTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header;
  Http::TestHeaderMapImpl response_header;
  Http::TestHeaderMapImpl response_trailer;

  envoy::api::v2::core::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"plain_string", "plain_string_value"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"plain_string", "plain_string_value"}};
  JsonFormatterImpl formatter(key_mapping);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterSingleOperatorTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header;
  Http::TestHeaderMapImpl response_header;
  Http::TestHeaderMapImpl response_trailer;

  envoy::api::v2::core::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  std::unordered_map<std::string, std::string> expected_json_map = {{"protocol", "HTTP/1.1"}};

  std::unordered_map<std::string, std::string> key_mapping = {{"protocol", "%PROTOCOL%"}};
  JsonFormatterImpl formatter(key_mapping);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterNonExistentHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
  Http::TestHeaderMapImpl response_header{{"some_response_header", "SOME_RESPONSE_HEADER"}};
  Http::TestHeaderMapImpl response_trailer;

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"protocol", "HTTP/1.1"},
      {"some_request_header", "SOME_REQUEST_HEADER"},
      {"nonexistent_response_header", "-"},
      {"some_response_header", "SOME_RESPONSE_HEADER"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"protocol", "%PROTOCOL%"},
      {"some_request_header", "%REQ(some_request_header)%"},
      {"nonexistent_response_header", "%RESP(nonexistent_response_header)%"},
      {"some_response_header", "%RESP(some_response_header)%"}};
  JsonFormatterImpl formatter(key_mapping);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterAlternateHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{"request_present_header", "REQUEST_PRESENT_HEADER"}};
  Http::TestHeaderMapImpl response_header{{"response_present_header", "RESPONSE_PRESENT_HEADER"}};
  Http::TestHeaderMapImpl response_trailer;

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"request_present_header_or_request_absent_header", "REQUEST_PRESENT_HEADER"},
      {"request_absent_header_or_request_present_header", "REQUEST_PRESENT_HEADER"},
      {"response_absent_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"},
      {"response_present_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"request_present_header_or_request_absent_header",
       "%REQ(request_present_header?request_absent_header)%"},
      {"request_absent_header_or_request_present_header",
       "%REQ(request_absent_header?request_present_header)%"},
      {"response_absent_header_or_response_absent_header",
       "%RESP(response_absent_header?response_present_header)%"},
      {"response_present_header_or_response_absent_header",
       "%RESP(response_present_header?response_absent_header)%"}};
  JsonFormatterImpl formatter(key_mapping);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestHeaderMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  envoy::api::v2::core::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"test_key", "\"test_value\""},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "\"inner_value\""}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"test_key", "%DYNAMIC_METADATA(com.test:test_key)%"},
      {"test_obj", "%DYNAMIC_METADATA(com.test:test_obj)%"},
      {"test_obj.inner_key", "%DYNAMIC_METADATA(com.test:test_obj:inner_key)%"}};

  JsonFormatterImpl formatter(key_mapping);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterStartTimeTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header;
  Http::TestHeaderMapImpl response_header;
  Http::TestHeaderMapImpl response_trailer;

  time_t test_epoch = 1522280158;
  SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));

  // Needed to take into account the behavior in non-GMT timezones.
  struct tm time_val;
  gmtime_r(&test_epoch, &time_val);
  time_t expected_time_t = mktime(&time_val);

  std::unordered_map<std::string, std::string> expected_json_map = {
      {"simple_date", "2018/03/28"},
      {"test_time", fmt::format("{}", expected_time_t)},
      {"bad_format", "bad_format"},
      {"default", "2018-03-28T23:35:58.000Z"},
      {"all_zeroes", "000000000.0.00.000"}};

  std::unordered_map<std::string, std::string> key_mapping = {
      {"simple_date", "%START_TIME(%Y/%m/%d)%"},
      {"test_time", "%START_TIME(%s)%"},
      {"bad_format", "%START_TIME(bad_format)%"},
      {"default", "%START_TIME%"},
      {"all_zeroes", "%START_TIME(%f.%1f.%2f.%3f)%"}};
  JsonFormatterImpl formatter(key_mapping);

  verifyJsonOutput(formatter.format(request_header, response_header, response_trailer, stream_info),
                   expected_json_map);
}

TEST(AccessLogFormatterTest, JsonFormatterMultiTokenTest) {
  {
    StreamInfo::MockStreamInfo stream_info;
    Http::TestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
    Http::TestHeaderMapImpl response_header{{"some_response_header", "SOME_RESPONSE_HEADER"}};
    Http::TestHeaderMapImpl response_trailer;

    std::unordered_map<std::string, std::string> expected_json_map = {
        {"multi_token_field", "HTTP/1.1 plainstring SOME_REQUEST_HEADER SOME_RESPONSE_HEADER"}};

    std::unordered_map<std::string, std::string> key_mapping = {
        {"multi_token_field",
         "%PROTOCOL% plainstring %REQ(some_request_header)% %RESP(some_response_header)%"}};
    JsonFormatterImpl formatter(key_mapping);

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

    const auto parsed = Json::Factory::loadFromString(
        formatter.format(request_header, response_header, response_trailer, stream_info));
    for (const auto& pair : expected_json_map) {
      EXPECT_EQ(parsed->getString(pair.first), pair.second);
    }
  }
}

TEST(AccessLogFormatterTest, CompositeFormatterSuccess) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestHeaderMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  {
    const std::string format = "{{%PROTOCOL%}}   %RESP(not exist)%++%RESP(test)% "
                               "%REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)%"
                               "\t@%TRAILER(THIRD)%@\t%TRAILER(TEST?TEST-2)%[]";
    FormatterImpl formatter(format);

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

    EXPECT_EQ("{{HTTP/1.1}}   -++test GET PUT\t@POST@\ttest-2[]",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "{}*JUST PLAIN string]";
    FormatterImpl formatter(format);

    EXPECT_EQ(format,
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "%REQ(first):3%|%REQ(first):1%|%RESP(first?second):2%|%REQ(first):"
                               "10%|%TRAILER(second?third):3%";

    FormatterImpl formatter(format);

    EXPECT_EQ("GET|G|PU|GET|POS",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    envoy::api::v2::core::Metadata metadata;
    populateMetadataTestData(metadata);
    EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    const std::string format = "%DYNAMIC_METADATA(com.test:test_key)%|%DYNAMIC_METADATA(com.test:"
                               "test_obj)%|%DYNAMIC_METADATA(com.test:test_obj:inner_key)%";
    FormatterImpl formatter(format);

    EXPECT_EQ("\"test_value\"|{\"inner_key\":\"inner_value\"}|\"inner_value\"",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format);

    // Needed to take into account the behavior in non-GMT timezones.
    struct tm time_val;
    gmtime_r(&test_epoch, &time_val);
    time_t expected_time_t = mktime(&time_val);

    EXPECT_EQ(fmt::format("2018/03/28|{}|bad_format|2018-03-28T23:35:58.000Z|000000000.0.00.000",
                          expected_time_t),
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests the beginning of time.
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%|%START_TIME(%f.%1f.%2f.%3f)%";

    const time_t test_epoch = 0;
    const SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format);

    EXPECT_EQ("1970/01/01|0|bad_format|1970-01-01T00:00:00.000Z|000000000.0.00.000",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests multiple START_TIMEs.
    const std::string format =
        "%START_TIME(%s.%3f)%|%START_TIME(%s.%4f)%|%START_TIME(%s.%5f)%|%START_TIME(%s.%6f)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("1522796769.123|1522796769.1234|1522796769.12345|1522796769.123456",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    const std::string format =
        "%START_TIME(segment1:%s.%3f|segment2:%s.%4f|seg3:%s.%6f|%s-%3f-asdf-%9f|.%7f:segm5:%Y)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("segment1:1522796769.123|segment2:1522796769.1234|seg3:1522796769.123456|1522796769-"
              "123-asdf-123456000|.1234560:segm5:2018",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }

  {
    // This tests START_TIME specifier that has shorter segments when formatted, i.e.
    // strftime("%%%%"") equals "%%", %1f will have 1 as its size.
    const std::string format = "%START_TIME(%%%%|%%%%%f|%s%%%%%3f|%1f%%%%%s)%";
    const SystemTime start_time(std::chrono::microseconds(1522796769123456));
    EXPECT_CALL(stream_info, startTime()).WillOnce(Return(start_time));
    FormatterImpl formatter(format);
    EXPECT_EQ("%%|%%123456000|1522796769%%123|1%%1522796769",
              formatter.format(request_header, response_header, response_trailer, stream_info));
  }
}

TEST(AccessLogFormatterTest, ParserFailures) {
  AccessLogFormatParser parser;

  std::vector<std::string> test_cases = {
      "{{%PROTOCOL%}}   ++ %REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)",
      "%REQ(FIRST?SECOND)T%",
      "RESP(FIRST)%",
      "%REQ(valid)% %NOT_VALID%",
      "%REQ(FIRST?SECOND%",
      "%%",
      "%protocol%",
      "%REQ(TEST):%",
      "%REQ(TEST):3q4%",
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
      "%DYNAMIC_METADATA(TEST"};

  for (const std::string& test_case : test_cases) {
    EXPECT_THROW(parser.parse(test_case), EnvoyException) << test_case;
  }
}

} // namespace AccessLog
} // namespace Envoy
