#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/stats/mocks.h"

using testing::_;

namespace Http {

class CodeUtilityTest : public testing::Test {
public:
  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING) {
    HeaderMapImpl headers{{":status", std::to_string(code)}};

    CodeUtility::ResponseStatInfo info{store_, "prefix.", headers, internal_request,
                                       request_vhost_name, request_vcluster_name, from_az, to_az,
                                       canary};

    CodeUtility::chargeResponseStat(info);
  }

  Stats::IsolatedStoreImpl store_;
};

TEST_F(CodeUtilityTest, NoCanary) {
  addResponse(201, false, false);
  addResponse(301, false, true);
  addResponse(401, false, false);
  addResponse(501, false, true);

  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_201").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_201").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_301").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_301").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_4xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_401").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_4xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_401").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_501").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_501").value());

  EXPECT_EQ(16U, store_.counters().size());
}

TEST_F(CodeUtilityTest, Canary) {
  addResponse(200, true, true);
  addResponse(300, false, false);
  addResponse(500, true, false);

  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_200").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.internal.upstream_rq_200").value());
  EXPECT_EQ(1U, store_.counter("prefix.canary.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_300").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_3xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_300").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.upstream_rq_500").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.external.upstream_rq_500").value());
  EXPECT_EQ(1U, store_.counter("prefix.canary.upstream_rq_5xx").value());
  EXPECT_EQ(1U, store_.counter("prefix.canary.upstream_rq_500").value());

  EXPECT_EQ(16U, store_.counters().size());
}

TEST_F(CodeUtilityTest, All) {
  std::vector<std::pair<Code, std::string>> testSet = {
      std::make_pair(Code::OK, "OK"), std::make_pair(Code::Created, "Created"),
      std::make_pair(Code::Accepted, "Accepted"),
      std::make_pair(Code::NonAuthoritativeInformation, "Non-Authoritative Information"),
      std::make_pair(Code::NoContent, "No Content"),
      std::make_pair(Code::ResetContent, "Reset Content"),
      std::make_pair(Code::PartialContent, "Partial Content"),
      std::make_pair(Code::MultiStatus, "Multi-Status"),
      std::make_pair(Code::AlreadyReported, "Already Reported"),
      std::make_pair(Code::IMUsed, "IM Used"),
      std::make_pair(Code::MultipleChoices, "Multiple Choices"),
      std::make_pair(Code::MovedPermanently, "Moved Permanently"),
      std::make_pair(Code::Found, "Found"), std::make_pair(Code::SeeOther, "See Other"),
      std::make_pair(Code::NotModified, "Not Modified"),
      std::make_pair(Code::UseProxy, "Use Proxy"),
      std::make_pair(Code::TemporaryRedirect, "Temporary Redirect"),
      std::make_pair(Code::PermanentRedirect, "Permanent Redirect"),
      std::make_pair(Code::BadRequest, "Bad Request"),
      std::make_pair(Code::Unauthorized, "Unauthorized"),
      std::make_pair(Code::PaymentRequired, "Payment Required"),
      std::make_pair(Code::Forbidden, "Forbidden"), std::make_pair(Code::NotFound, "Not Found"),
      std::make_pair(Code::MethodNotAllowed, "Method Not Allowed"),
      std::make_pair(Code::NotAcceptable, "Not Acceptable"),
      std::make_pair(Code::ProxyAuthenticationRequired, "Proxy Authentication Required"),
      std::make_pair(Code::RequestTimeout, "Request Timeout"),
      std::make_pair(Code::Conflict, "Conflict"), std::make_pair(Code::Gone, "Gone"),
      std::make_pair(Code::LengthRequired, "Length Required"),
      std::make_pair(Code::PreconditionFailed, "Precondition Failed"),
      std::make_pair(Code::PayloadTooLarge, "Payload Too Large"),
      std::make_pair(Code::URITooLong, "URI Too Long"),
      std::make_pair(Code::UnsupportedMediaType, "Unsupported Media Type"),
      std::make_pair(Code::RangeNotSatisfiable, "Range Not Satisfiable"),
      std::make_pair(Code::ExpectationFailed, "Expectation Failed"),
      std::make_pair(Code::MisdirectedRequest, "Misdirected Request"),
      std::make_pair(Code::UnprocessableEntity, "Unprocessable Entity"),
      std::make_pair(Code::Locked, "Locked"),
      std::make_pair(Code::FailedDependency, "Failed Dependency"),
      std::make_pair(Code::UpgradeRequired, "Upgrade Required"),
      std::make_pair(Code::PreconditionRequired, "Precondition Required"),
      std::make_pair(Code::TooManyRequests, "Too Many Requests"),
      std::make_pair(Code::RequestHeaderFieldsTooLarge, "Request Header Fields Too Large"),
      std::make_pair(Code::InternalServerError, "Internal Server Error"),
      std::make_pair(Code::NotImplemented, "Not Implemented"),
      std::make_pair(Code::BadGateway, "Bad Gateway"),
      std::make_pair(Code::ServiceUnavailable, "Service Unavailable"),
      std::make_pair(Code::GatewayTimeout, "Gateway Timeout"),
      std::make_pair(Code::HTTPVersionNotSupported, "HTTP Version Not Supported"),
      std::make_pair(Code::VariantAlsoNegotiates, "Variant Also Negotiates"),
      std::make_pair(Code::InsufficientStorage, "Insufficient Storage"),
      std::make_pair(Code::LoopDetected, "Loop Detected"),
      std::make_pair(Code::NotExtended, "Not Extended"),
      std::make_pair(Code::NetworkAuthenticationRequired, "Network Authentication Required")};

  for (const auto& testCase : testSet) {
    EXPECT_EQ(testCase.second, CodeUtility::toString(testCase.first));
  }

  EXPECT_EQ(std::string("Unknown"), CodeUtility::toString(static_cast<Code>(600)));
}

TEST_F(CodeUtilityTest, RequestVirtualCluster) {
  addResponse(200, false, false, "test-vhost", "test-cluster");

  EXPECT_EQ(1U, store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_2xx").value());
  EXPECT_EQ(1U, store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_200").value());
}

TEST_F(CodeUtilityTest, PerZoneStats) {
  addResponse(200, false, false, "", "", "from_az", "to_az");

  EXPECT_EQ(1U, store_.counter("prefix.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(1U, store_.counter("prefix.zone.from_az.to_az.upstream_rq_2xx").value());
}

TEST(CodeUtilityResponseTimingTest, All) {
  Stats::MockStore store;

  CodeUtility::ResponseTimingInfo info{store, "prefix.", std::chrono::milliseconds(5), true, true,
                                       "vhost_name", "req_vcluster_name", "from_az", "to_az"};

  EXPECT_CALL(store, deliverTimingToSinks("prefix.upstream_rq_time", std::chrono::milliseconds(5)));
  EXPECT_CALL(store,
              deliverTimingToSinks("prefix.canary.upstream_rq_time", std::chrono::milliseconds(5)));
  EXPECT_CALL(store, deliverTimingToSinks("prefix.internal.upstream_rq_time",
                                          std::chrono::milliseconds(5)));
  EXPECT_CALL(store,
              deliverTimingToSinks("vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time",
                                   std::chrono::milliseconds(5)));
  EXPECT_CALL(store, deliverTimingToSinks("prefix.zone.from_az.to_az.upstream_rq_time",
                                          std::chrono::milliseconds(5)));
  CodeUtility::chargeResponseTiming(info);
}

} // Http
