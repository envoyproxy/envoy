#include "common/common/utility.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Http {
namespace AccessLog {

TEST(ResponseFlagUtilsTest, toShortStringConversion) {
  std::vector<std::pair<ResponseFlag, std::string>> expected = {
      std::make_pair(ResponseFlag::FailedLocalHealthCheck, "LH"),
      std::make_pair(ResponseFlag::NoHealthyUpstream, "UH"),
      std::make_pair(ResponseFlag::UpstreamRequestTimeout, "UT"),
      std::make_pair(ResponseFlag::LocalReset, "LR"),
      std::make_pair(ResponseFlag::UpstreamRemoteReset, "UR"),
      std::make_pair(ResponseFlag::UpstreamConnectionFailure, "UF"),
      std::make_pair(ResponseFlag::UpstreamConnectionTermination, "UC"),
      std::make_pair(ResponseFlag::UpstreamOverflow, "UO"),
      std::make_pair(ResponseFlag::NoRouteFound, "NR"),
      std::make_pair(ResponseFlag::FaultInjected, "DI"),
      std::make_pair(ResponseFlag::FaultInjected, "FI")};

  for (const auto& testCase : expected) {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, getResponseFlag(testCase.first)).WillByDefault(Return(true));
    EXPECT_EQ(testCase.second, ResponseFlagUtils::toShortString(request_info));
  }

  // No flag is set.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, getResponseFlag(_)).WillByDefault(Return(false));
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(request_info));
  }

  // Test combinations.
  // These are not real use cases, but are used to cover multiple response flags case.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, getResponseFlag(ResponseFlag::DelayInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, getResponseFlag(ResponseFlag::FaultInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, getResponseFlag(ResponseFlag::UpstreamRequestTimeout))
        .WillByDefault(Return(true));
    EXPECT_EQ("UT,DI,FI", ResponseFlagUtils::toShortString(request_info));
  }
}

TEST(AccessLogFormatUtilsTest, protocolToString) {
  EXPECT_EQ("HTTP/1.0", AccessLogFormatUtils::protocolToString(Protocol::Http10));
  EXPECT_EQ("HTTP/1.1", AccessLogFormatUtils::protocolToString(Protocol::Http11));
  EXPECT_EQ("HTTP/2", AccessLogFormatUtils::protocolToString(Protocol::Http2));
}

TEST(AccessLogFormatterTest, plainStringFormatter) {
  PlainStringFormatter formatter("plain");
  TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};
  MockRequestInfo request_info;

  EXPECT_EQ("plain", formatter.format(header, header, request_info));
}

TEST(AccessLogFormatterTest, requestInfoFormatter) {
  EXPECT_THROW(RequestInfoFormatter formatter("unknown_field"), EnvoyException);

  NiceMock<MockRequestInfo> requestInfo;
  TestHeaderMapImpl header{{":method", "GET"}, {":path", "/"}};

  {
    RequestInfoFormatter start_time_format("START_TIME");
    SystemTime time;
    EXPECT_CALL(requestInfo, startTime()).WillOnce(Return(time));
    EXPECT_EQ(AccessLogDateTimeFormatter::fromTime(time),
              start_time_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter bytes_received_format("BYTES_RECEIVED");
    EXPECT_CALL(requestInfo, bytesReceived()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_received_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter protocol_format("PROTOCOL");
    EXPECT_CALL(requestInfo, protocol()).WillOnce(Return(Protocol::Http11));
    EXPECT_EQ("HTTP/1.1", protocol_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter response_format("RESPONSE_CODE");
    Optional<uint32_t> response_code{200};
    EXPECT_CALL(requestInfo, responseCode()).WillRepeatedly(ReturnRef(response_code));
    EXPECT_EQ("200", response_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter response_code_format("RESPONSE_CODE");
    Optional<uint32_t> response_code;
    EXPECT_CALL(requestInfo, responseCode()).WillRepeatedly(ReturnRef(response_code));
    EXPECT_EQ("0", response_code_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter bytes_sent_format("BYTES_SENT");
    EXPECT_CALL(requestInfo, bytesSent()).WillOnce(Return(1));
    EXPECT_EQ("1", bytes_sent_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter duration_format("DURATION");
    std::chrono::milliseconds time{2};
    EXPECT_CALL(requestInfo, duration()).WillOnce(Return(time));
    EXPECT_EQ("2", duration_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter response_flags_format("RESPONSE_FLAGS");
    ON_CALL(requestInfo, getResponseFlag(ResponseFlag::LocalReset)).WillByDefault(Return(true));
    EXPECT_EQ("LR", response_flags_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_HOST");
    std::shared_ptr<Upstream::MockHostDescription> host(new Upstream::MockHostDescription());
    EXPECT_CALL(requestInfo, upstreamHost()).WillRepeatedly(Return(host));
    const std::string host_url = "name";
    EXPECT_CALL(*host, url()).WillOnce(ReturnRef(host_url));
    EXPECT_EQ("name", upstream_format.format(header, header, requestInfo));
  }

  {
    RequestInfoFormatter upstream_format("UPSTREAM_HOST");
    EXPECT_CALL(requestInfo, upstreamHost()).WillOnce(Return(nullptr));
    EXPECT_EQ("-", upstream_format.format(header, header, requestInfo));
  }
}

TEST(AccessLogFormatterTest, requestHeaderFormatter) {
  MockRequestInfo requestInfo;
  TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  TestHeaderMapImpl response_header{{":method", "PUT"}};

  {
    RequestHeaderFormatter formatter(":Method", "", Optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, requestInfo));
  }

  {
    RequestHeaderFormatter formatter(":path", ":method", Optional<size_t>());
    EXPECT_EQ("/", formatter.format(request_header, response_header, requestInfo));
  }

  {
    RequestHeaderFormatter formatter(":TEST", ":METHOD", Optional<size_t>());
    EXPECT_EQ("GET", formatter.format(request_header, response_header, requestInfo));
  }

  {
    RequestHeaderFormatter formatter("does_not_exist", "", Optional<size_t>());
    EXPECT_EQ("-", formatter.format(request_header, response_header, requestInfo));
  }
}

TEST(AccessLogFormatterTest, responseHeaderFormatter) {
  MockRequestInfo requestInfo;
  TestHeaderMapImpl request_header{{":method", "GET"}, {":path", "/"}};
  TestHeaderMapImpl response_header{{":method", "PUT"}, {"test", "test"}};

  {
    ResponseHeaderFormatter formatter(":method", "", Optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, requestInfo));
  }

  {
    ResponseHeaderFormatter formatter("test", ":method", Optional<size_t>());
    EXPECT_EQ("test", formatter.format(request_header, response_header, requestInfo));
  }

  {
    ResponseHeaderFormatter formatter(":path", ":method", Optional<size_t>());
    EXPECT_EQ("PUT", formatter.format(request_header, response_header, requestInfo));
  }

  {
    ResponseHeaderFormatter formatter("does_not_exist", "", Optional<size_t>());
    EXPECT_EQ("-", formatter.format(request_header, response_header, requestInfo));
  }
}

TEST(AccessLogFormatterTest, CompositeFormatterSuccess) {
  MockRequestInfo request_info;
  TestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  TestHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};

  {
    const std::string format = "{{%PROTOCOL%}}   %RESP(not exist)%++%RESP(test)% "
                               "%REQ(FIRST?SECOND)% %RESP(FIRST?SECOND)%[]";
    FormatterImpl formatter(format);

    Protocol protocol = Protocol::Http11;
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

} // AccessLog
} // Http
