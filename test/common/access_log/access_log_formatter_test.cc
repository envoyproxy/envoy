#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/access_log/access_log_formatter.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/request_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

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
  RequestInfo::MockRequestInfo request_info;

  EXPECT_EQ("plain", formatter.format(header, header, request_info));
}

TEST(AccessLogFormatterTest, requestInfoFormatter) {
  EXPECT_THROW(RequestInfoFormatter formatter("unknown_field"), EnvoyException);

  NiceMock<RequestInfo::MockRequestInfo> request_info;
  Http::TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};

  {
    RequestInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(5000000);
    EXPECT_CALL(request_info, lastDownstreamRxByteReceived()).WillOnce(Return(dur));
    EXPECT_EQ("5", request_duration_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter request_duration_format("REQUEST_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(request_info, lastDownstreamRxByteReceived()).WillOnce(Return(dur));
    EXPECT_EQ("-", request_duration_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(10000000);
    EXPECT_CALL(request_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("10", response_duration_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter response_duration_format("RESPONSE_DURATION");
    absl::optional<std::chrono::nanoseconds> dur;
    EXPECT_CALL(request_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(dur));
    EXPECT_EQ("-", response_duration_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter bytes_received_format("BYTES_RECEIVED");
    EXPECT_CALL(request_info, bytesReceived()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_received_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter protocol_format("PROTOCOL");
    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(request_info, protocol()).WillOnce(Return(protocol));
    EXPECT_EQ("HTTP/1.1", protocol_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter response_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code{200};
    EXPECT_CALL(request_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("200", response_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter response_code_format("RESPONSE_CODE");
    absl::optional<uint32_t> response_code;
    EXPECT_CALL(request_info, responseCode()).WillRepeatedly(Return(response_code));
    EXPECT_EQ("0", response_code_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter bytes_sent_format("BYTES_SENT");
    EXPECT_CALL(request_info, bytesSent()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_sent_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter duration_format("DURATION");
    absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
    EXPECT_CALL(request_info, requestComplete()).WillRepeatedly(Return(dur));
    EXPECT_EQ("15", duration_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter response_flags_format("RESPONSE_FLAGS");
    ON_CALL(request_info, getResponseFlag(RequestInfo::ResponseFlag::LocalReset))
        .WillByDefault(Return(true));
    EXPECT_EQ("LR", response_flags_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_EQ("10.0.0.1:443", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    const std::string upstream_cluster_name = "cluster_name";
    EXPECT_CALL(request_info.host_->cluster_, name()).WillOnce(ReturnRef(upstream_cluster_name));
    EXPECT_EQ("cluster_name", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_CALL(request_info, upstreamHost()).WillOnce(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_CLUSTER");
    EXPECT_CALL(request_info, upstreamHost()).WillOnce(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS");
    EXPECT_EQ("127.0.0.2:0", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.2", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("DOWNSTREAM_ADDRESS");
    EXPECT_EQ("127.0.0.1", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT");
    EXPECT_EQ("127.0.0.1", upstream_format.format(header, header, request_info));
  }

  {
    RequestInfoFormatter upstream_format("DOWNSTREAM_REMOTE_ADDRESS");
    EXPECT_EQ("127.0.0.1:0", upstream_format.format(header, header, request_info));
  }
}

TEST(AccessLogFormatterTest, requestHeaderFormatter) {
  RequestInfo::MockRequestInfo request_info;
  Http::TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{":method", "PUT"}};

  {
    RequestHeaderFormatter formatter(":Method", "", absl::optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, request_info));
  }

  {
    RequestHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("/", formatter.format(request_header, response_header, request_info));
  }

  {
    RequestHeaderFormatter formatter(":TEST", ":METHOD", absl::optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, request_info));
  }

  {
    RequestHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-", formatter.format(request_header, response_header, request_info));
  }
}

TEST(AccessLogFormatterTest, responseHeaderFormatter) {
  RequestInfo::MockRequestInfo request_info;
  Http::TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};

  {
    ResponseHeaderFormatter formatter(":method", "", absl::optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, request_info));
  }

  {
    ResponseHeaderFormatter formatter("test", ":method", absl::optional<size_t>());
    EXPECT_EQ("test", formatter.format(request_header, response_header, request_info));
  }

  {
    ResponseHeaderFormatter formatter(":path", ":method", absl::optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, request_info));
  }

  {
    ResponseHeaderFormatter formatter("does_not_exist", "", absl::optional<size_t>());
    EXPECT_EQ("-", formatter.format(request_header, response_header, request_info));
  }
}

TEST(AccessLogFormatterTest, startTimeFormatter) {
  NiceMock<RequestInfo::MockRequestInfo> request_info;
  Http::TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};

  {
    StartTimeFormatter start_time_format("%Y/%m/%d");
    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(request_info, startTime()).WillOnce(Return(time));
    EXPECT_EQ("2018/03/28", start_time_format.format(header, header, request_info));
  }

  {
    StartTimeFormatter start_time_format("");
    SystemTime time;
    EXPECT_CALL(request_info, startTime()).WillOnce(Return(time));
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              start_time_format.format(header, header, request_info));
  }
}

TEST(AccessLogFormatterTest, CompositeFormatterSuccess) {
  RequestInfo::MockRequestInfo request_info;
  Http::TestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};

  {
    const std::string format = "{{%PROTOCOL%}}   %RESP(not exist)%++%RESP(test)% "
                               "%REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)%[]";
    FormatterImpl formatter(format);

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(request_info, protocol()).WillRepeatedly(Return(protocol));

    EXPECT_EQ("{{HTTP/1.1}}   -++test GET PUT[]",
              formatter.format(request_header, response_header, request_info));
  }

  {
    const std::string format = "{}*JUST PLAIN string]";
    FormatterImpl formatter(format);

    EXPECT_EQ(format, formatter.format(request_header, response_header, request_info));
  }

  {
    const std::string format =
        "%REQ(first):3%|%REQ(first):1%|%RESP(first?second):2%|%REQ(first):10%";

    FormatterImpl formatter(format);

    EXPECT_EQ("GET|G|PU|GET", formatter.format(request_header, response_header, request_info));
  }

  {
    const std::string format = "%START_TIME(%Y/%m/%d)%|%START_TIME(%s)%|%START_TIME(bad_format)%|"
                               "%START_TIME%";

    time_t test_epoch = 1522280158;
    SystemTime time = std::chrono::system_clock::from_time_t(test_epoch);
    EXPECT_CALL(request_info, startTime()).WillRepeatedly(Return(time));
    FormatterImpl formatter(format);

    EXPECT_EQ("2018/03/28|1522280158|bad_format|2018-03-28T23:35:58.000Z",
              formatter.format(request_header, response_header, request_info));
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
      "%REQ("};

  for (const std::string& test_case : test_cases) {
    EXPECT_THROW(parser.parse(test_case), EnvoyException);
  }
}

} // namespace AccessLog
} // namespace Envoy
