#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/stats.h"

#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Property;

namespace Envoy {
namespace Http {

class CodeUtilityTest : public testing::Test {
public:
  CodeUtilityTest()
      : global_store_(*symbol_table_), cluster_scope_(*symbol_table_), code_stats_(*symbol_table_),
        pool_(*symbol_table_) {}

  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING) {
    Stats::StatName prefix = pool_.add("prefix");
    Stats::StatName from_zone = pool_.add(from_az);
    Stats::StatName to_zone = pool_.add(to_az);
    Stats::StatName vhost_name = pool_.add(request_vhost_name);
    Stats::StatName vcluster_name = pool_.add(request_vcluster_name);
    Http::CodeStats::ResponseStatInfo info{
        global_store_, cluster_scope_, prefix,    code,    internal_request,
        vhost_name,    vcluster_name,  from_zone, to_zone, canary};

    code_stats_.chargeResponseStat(info);
  }

  Stats::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore global_store_;
  Stats::TestUtil::TestStore cluster_scope_;
  Http::CodeStatsImpl code_stats_;
  Stats::StatNamePool pool_;
};

TEST_F(CodeUtilityTest, GroupStrings) {
  EXPECT_EQ("1xx", CodeUtility::groupStringForResponseCode(Code::SwitchingProtocols));
  EXPECT_EQ("2xx", CodeUtility::groupStringForResponseCode(Code::OK));
  EXPECT_EQ("3xx", CodeUtility::groupStringForResponseCode(Code::Found));
  EXPECT_EQ("4xx", CodeUtility::groupStringForResponseCode(Code::NotFound));
  EXPECT_EQ("5xx", CodeUtility::groupStringForResponseCode(Code::NotImplemented));
  EXPECT_EQ("", CodeUtility::groupStringForResponseCode(static_cast<Code>(600)));
}

TEST_F(CodeUtilityTest, NoCanary) {
  addResponse(201, false, false);
  addResponse(301, false, true);
  addResponse(401, false, false);
  addResponse(501, false, true);

  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_501").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_501").value());

  EXPECT_EQ(4U, cluster_scope_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_scope_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_scope_.counter("prefix.internal.upstream_rq_completed").value());

  EXPECT_EQ(19U, cluster_scope_.counters().size());
}

TEST_F(CodeUtilityTest, Canary) {
  addResponse(100, true, true);
  addResponse(200, true, true);
  addResponse(300, false, false);
  addResponse(500, true, false);

  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_100").value());

  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.internal.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_500").value());

  EXPECT_EQ(4U, cluster_scope_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_scope_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_scope_.counter("prefix.internal.upstream_rq_completed").value());
  EXPECT_EQ(3U, cluster_scope_.counter("prefix.canary.upstream_rq_completed").value());

  EXPECT_EQ(26U, cluster_scope_.counters().size());
}

TEST_F(CodeUtilityTest, UnknownResponseCodes) {
  addResponse(23, true, true);
  addResponse(600, false, false);
  addResponse(1000000, false, true);

  EXPECT_EQ(3U, cluster_scope_.counter("prefix.upstream_rq_unknown").value());
  EXPECT_EQ(2U, cluster_scope_.counter("prefix.internal.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.canary.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.external.upstream_rq_unknown").value());

  EXPECT_EQ(8U, cluster_scope_.counters().size());
}

TEST_F(CodeUtilityTest, All) {
  const std::vector<std::pair<Code, std::string>> test_set = {
      std::make_pair(Code::Continue, "Continue"),
      std::make_pair(Code::SwitchingProtocols, "Switching Protocols"),
      std::make_pair(Code::OK, "OK"),
      std::make_pair(Code::Created, "Created"),
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
      std::make_pair(Code::Found, "Found"),
      std::make_pair(Code::SeeOther, "See Other"),
      std::make_pair(Code::NotModified, "Not Modified"),
      std::make_pair(Code::UseProxy, "Use Proxy"),
      std::make_pair(Code::TemporaryRedirect, "Temporary Redirect"),
      std::make_pair(Code::PermanentRedirect, "Permanent Redirect"),
      std::make_pair(Code::BadRequest, "Bad Request"),
      std::make_pair(Code::Unauthorized, "Unauthorized"),
      std::make_pair(Code::PaymentRequired, "Payment Required"),
      std::make_pair(Code::Forbidden, "Forbidden"),
      std::make_pair(Code::NotFound, "Not Found"),
      std::make_pair(Code::MethodNotAllowed, "Method Not Allowed"),
      std::make_pair(Code::NotAcceptable, "Not Acceptable"),
      std::make_pair(Code::ProxyAuthenticationRequired, "Proxy Authentication Required"),
      std::make_pair(Code::RequestTimeout, "Request Timeout"),
      std::make_pair(Code::Conflict, "Conflict"),
      std::make_pair(Code::Gone, "Gone"),
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
      std::make_pair(Code::NetworkAuthenticationRequired, "Network Authentication Required"),
      std::make_pair(static_cast<Code>(600), "Unknown")};

  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, CodeUtility::toString(test_case.first));
  }

  EXPECT_EQ(std::string("Unknown"), CodeUtility::toString(static_cast<Code>(600)));
}

TEST_F(CodeUtilityTest, RequestVirtualCluster) {
  addResponse(200, false, false, "test-vhost", "test-cluster");

  EXPECT_EQ(1U,
            global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed")
                .value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_2xx").value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_200").value());
}

TEST_F(CodeUtilityTest, PerZoneStats) {
  addResponse(200, false, false, "", "", "from_az", "to_az");

  EXPECT_EQ(1U, cluster_scope_.counter("prefix.zone.from_az.to_az.upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_scope_.counter("prefix.zone.from_az.to_az.upstream_rq_2xx").value());
}

TEST_F(CodeUtilityTest, ResponseTimingTest) {
  Stats::MockStore global_store;
  Stats::MockStore cluster_scope;

  Stats::StatNameManagedStorage prefix("prefix", *symbol_table_);
  Http::CodeStats::ResponseTimingInfo info{global_store,
                                           cluster_scope,
                                           pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           pool_.add("vhost_name"),
                                           pool_.add("req_vcluster_name"),
                                           pool_.add("from_az"),
                                           pool_.add("to_az")};

  EXPECT_CALL(cluster_scope,
              histogram("prefix.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_scope, deliverHistogramToSinks(
                                 Property(&Stats::Metric::name, "prefix.upstream_rq_time"), 5));

  EXPECT_CALL(cluster_scope,
              histogram("prefix.canary.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(
      cluster_scope,
      deliverHistogramToSinks(Property(&Stats::Metric::name, "prefix.canary.upstream_rq_time"), 5));

  EXPECT_CALL(cluster_scope,
              histogram("prefix.internal.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_scope,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "prefix.internal.upstream_rq_time"), 5));
  EXPECT_CALL(global_store,
              histogram("vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time",
                        Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(global_store,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name,
                           "vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time"),
                  5));

  EXPECT_CALL(cluster_scope, histogram("prefix.zone.from_az.to_az.upstream_rq_time",
                                       Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_scope,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "prefix.zone.from_az.to_az.upstream_rq_time"), 5));
  Http::CodeStatsImpl code_stats(*symbol_table_);
  code_stats.chargeResponseTiming(info);
}

} // namespace Http
} // namespace Envoy
