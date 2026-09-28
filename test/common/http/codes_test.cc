#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/stats.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stats/tag_producer_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/enum_test_utils.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Property;

namespace Envoy {
namespace Http {

enum class CodeStatsImplementation { Legacy, Tagged };

std::string implementationName(const testing::TestParamInfo<CodeStatsImplementation>& info) {
  return info.param == CodeStatsImplementation::Legacy ? "Legacy" : "Tagged";
}

class CodeUtilityTest : public testing::TestWithParam<CodeStatsImplementation> {
public:
  CodeUtilityTest()
      : global_store_(*symbol_table_), cluster_store_(*symbol_table_),
        code_stats_(makeCodeStats(GetParam(), *symbol_table_)), pool_(*symbol_table_) {}

  static std::unique_ptr<CodeStats> makeCodeStats(CodeStatsImplementation implementation,
                                                  Stats::SymbolTable& symbol_table) {
    if (implementation == CodeStatsImplementation::Legacy) {
      return std::make_unique<CodeStatsImpl>(symbol_table);
    }
    return std::make_unique<TaggedCodeStatsImpl>(symbol_table);
  }

  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING,
                   const std::string& request_route_name = EMPTY_STRING) {
    Stats::StatName prefix = pool_.add(prefix_);
    Stats::StatName from_zone = pool_.add(from_az);
    Stats::StatName to_zone = pool_.add(to_az);
    Stats::StatName vhost_name = pool_.add(request_vhost_name);
    Stats::StatName vcluster_name = pool_.add(request_vcluster_name);
    Stats::StatName route_name = pool_.add(request_route_name);
    Http::CodeStats::ResponseStatInfo info{*global_store_.rootScope(),
                                           *cluster_store_.rootScope(),
                                           prefix,
                                           code,
                                           internal_request,
                                           vhost_name,
                                           vcluster_name,
                                           route_name,
                                           from_zone,
                                           to_zone,
                                           canary};

    code_stats_->chargeResponseStat(info, false);
  }

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore global_store_;
  Stats::TestUtil::TestStore cluster_store_;
  std::unique_ptr<CodeStats> code_stats_;
  Stats::StatNamePool pool_;
  // ResponseStatInfo::prefix_; empty for the router, ext_authz and ratelimit call sites.
  std::string prefix_{"prefix"};
};

INSTANTIATE_TEST_SUITE_P(Implementations, CodeUtilityTest,
                         testing::Values(CodeStatsImplementation::Legacy,
                                         CodeStatsImplementation::Tagged),
                         implementationName);

TEST_P(CodeUtilityTest, GroupStrings) {
  EXPECT_EQ("1xx", CodeUtility::groupStringForResponseCode(Code::SwitchingProtocols));
  EXPECT_EQ("2xx", CodeUtility::groupStringForResponseCode(Code::OK));
  EXPECT_EQ("3xx", CodeUtility::groupStringForResponseCode(Code::Found));
  EXPECT_EQ("4xx", CodeUtility::groupStringForResponseCode(Code::NotFound));
  EXPECT_EQ("5xx", CodeUtility::groupStringForResponseCode(Code::NotImplemented));
  EXPECT_EQ("", CodeUtility::groupStringForResponseCode(uncheckedEnumCastForTest<Code>(600)));
}

TEST_P(CodeUtilityTest, NoCanary) {
  addResponse(201, false, false);
  addResponse(301, false, true);
  addResponse(401, false, false);
  addResponse(501, false, true);

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_4xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_401").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_501").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_501").value());

  EXPECT_EQ(4U, cluster_store_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_completed").value());

  EXPECT_EQ(19U, cluster_store_.counters().size());
}

// The router, ext_authz and ratelimit all charge response stats with an empty prefix. The
// resulting names must match the non-empty-prefix shape minus the prefix, with no stray dot.
TEST_P(CodeUtilityTest, EmptyPrefix) {
  prefix_.clear();
  addResponse(201, false, false);
  addResponse(301, false, true);

  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_201").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("upstream_rq_301").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_301").value());

  EXPECT_EQ(2U, cluster_store_.counter("upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("external.upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("internal.upstream_rq_completed").value());

  EXPECT_EQ(11U, cluster_store_.counters().size());
}

TEST_P(CodeUtilityTest, Canary) {
  addResponse(100, true, true);
  addResponse(200, true, true);
  addResponse(300, false, false);
  addResponse(500, true, false);

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_100").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_1xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_100").value());

  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.internal.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_2xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_3xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_300").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_500").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_5xx").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_500").value());

  EXPECT_EQ(4U, cluster_store_.counter("prefix.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.external.upstream_rq_completed").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_completed").value());
  EXPECT_EQ(3U, cluster_store_.counter("prefix.canary.upstream_rq_completed").value());

  EXPECT_EQ(26U, cluster_store_.counters().size());
}

TEST_P(CodeUtilityTest, UnknownResponseCodes) {
  addResponse(23, true, true);
  addResponse(600, false, false);
  addResponse(1000000, false, true);

  EXPECT_EQ(3U, cluster_store_.counter("prefix.upstream_rq_unknown").value());
  EXPECT_EQ(2U, cluster_store_.counter("prefix.internal.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.canary.upstream_rq_unknown").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.external.upstream_rq_unknown").value());

  EXPECT_EQ(8U, cluster_store_.counters().size());
}

TEST_P(CodeUtilityTest, All) {
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
      std::make_pair(uncheckedEnumCastForTest<Code>(600), "Unknown")};

  for (const auto& test_case : test_set) {
    EXPECT_EQ(test_case.second, CodeUtility::toString(test_case.first));
  }

  EXPECT_EQ(std::string("Unknown"), CodeUtility::toString(uncheckedEnumCastForTest<Code>(600)));
}

TEST_P(CodeUtilityTest, RequestVirtualCluster) {
  addResponse(200, false, false, "test-vhost", "test-cluster");

  EXPECT_EQ(1U,
            global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed")
                .value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_2xx").value());
  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.vcluster.test-cluster.upstream_rq_200").value());
}

TEST_P(CodeUtilityTest, PerZoneStats) {
  addResponse(200, false, false, "", "", "from_az", "to_az");

  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_completed").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(1U, cluster_store_.counter("prefix.zone.from_az.to_az.upstream_rq_2xx").value());
}

TEST_P(CodeUtilityTest, ResponseTimingTest) {
  Stats::MockStore global_store;
  Stats::MockStore cluster_scope;
  Stats::StatName empty_stat_name;

  Stats::StatNameManagedStorage prefix("prefix", *symbol_table_);
  Http::CodeStats::ResponseTimingInfo info{*global_store.rootScope(),
                                           *cluster_scope.rootScope(),
                                           pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           pool_.add("vhost_name"),
                                           pool_.add("req_vcluster_name"),
                                           empty_stat_name,
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
  code_stats_->chargeResponseTiming(info);
}

TEST_P(CodeUtilityTest, RequestRoute) {
  addResponse(200, false, false, "test-vhost", "", "", "", "test-route");

  EXPECT_EQ(
      1U, global_store_.counter("vhost.test-vhost.route.test-route.upstream_rq_completed").value());
  EXPECT_EQ(1U, global_store_.counter("vhost.test-vhost.route.test-route.upstream_rq_2xx").value());
  EXPECT_EQ(1U, global_store_.counter("vhost.test-vhost.route.test-route.upstream_rq_200").value());
}

// The route timing histogram, which ResponseTimingTest above does not reach as it leaves the route
// name empty.
TEST_P(CodeUtilityTest, ResponseTimingWithRoute) {
  Http::CodeStats::ResponseTimingInfo info{*global_store_.rootScope(),
                                           *cluster_store_.rootScope(),
                                           pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           pool_.add("vhost_name"),
                                           pool_.add("req_vcluster_name"),
                                           pool_.add("route_name"),
                                           pool_.add("from_az"),
                                           pool_.add("to_az")};

  code_stats_->chargeResponseTiming(info);

  const std::vector<uint64_t> five{5};
  EXPECT_EQ(five, global_store_.histogramValues(
                      "vhost.vhost_name.route.route_name.upstream_rq_time", false));
  EXPECT_EQ(five, global_store_.histogramValues(
                      "vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time", false));
}

using TagPairs = std::vector<std::pair<std::string, std::string>>;

// The tags of a stat, sorted, so that they compare equal however they were ordered. Tag order is
// not part of a stat's identity, and the explicit tags are attached in a different order than the
// tag extraction rules happen to produce them in.
TagPairs sortedTags(const Stats::TagVector& tags) {
  TagPairs pairs;
  pairs.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    pairs.emplace_back(tag.name_, tag.value_);
  }
  std::sort(pairs.begin(), pairs.end());
  return pairs;
}

// The tags TaggedCodeStatsImpl attaches to the stats it charges are observable on the stats
// themselves, alongside the name each stat is tag-extracted to.
class TaggedCodeStatsTest : public CodeUtilityTest {
public:
  static void expectMetric(const Stats::Metric& metric, const std::string& name,
                           const std::string& tag_extracted_name, TagPairs tags) {
    EXPECT_EQ(metric.tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";
    std::sort(tags.begin(), tags.end());
    EXPECT_EQ(sortedTags(metric.tags()), tags) << " for stat '" << name << "'";
  }

  static void expectCounter(Stats::TestUtil::TestStore& store, const std::string& name,
                            const std::string& tag_extracted_name, TagPairs tags, uint64_t value) {
    Stats::CounterOptConstRef counter = store.findCounterByString(name);
    ASSERT_TRUE(counter.has_value()) << "no counter named '" << name << "'";
    EXPECT_EQ(counter->get().value(), value) << " for stat '" << name << "'";
    expectMetric(counter->get(), name, tag_extracted_name, std::move(tags));
  }

  static void expectHistogram(Stats::TestUtil::TestStore& store, const std::string& name,
                              const std::string& tag_extracted_name, TagPairs tags,
                              const std::vector<uint64_t>& values) {
    Stats::HistogramOptConstRef histogram = store.findHistogramByString(name);
    ASSERT_TRUE(histogram.has_value()) << "no histogram named '" << name << "'";
    EXPECT_EQ(store.histogramValues(name, false), values) << " for stat '" << name << "'";
    expectMetric(histogram->get(), name, tag_extracted_name, std::move(tags));
  }

  const std::string& response_code_tag_{Config::TagNames::get().RESPONSE_CODE};
  const std::string& response_code_class_tag_{Config::TagNames::get().RESPONSE_CODE_CLASS};
  const std::string& route_tag_{Config::TagNames::get().ROUTE};
  const std::string& virtual_cluster_tag_{Config::TagNames::get().VIRTUAL_CLUSTER};
  const std::string& virtual_host_tag_{Config::TagNames::get().VIRTUAL_HOST};
};

INSTANTIATE_TEST_SUITE_P(Tagged, TaggedCodeStatsTest,
                         testing::Values(CodeStatsImplementation::Tagged), implementationName);

TEST_P(TaggedCodeStatsTest, ExplicitTags) {
  addResponse(200, true, false, "test-vhost", "test-cluster", "from_az", "to_az", "test-route");
  addResponse(600, false, true);

  // The response code and its class are tags of their own, so the tag-extracted name drops the
  // code, and drops the class while keeping the 'xx' that surrounds it.
  expectCounter(cluster_store_, "prefix.upstream_rq_200", "prefix.upstream_rq",
                {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.upstream_rq_2xx", "prefix.upstream_rq_xx",
                {{response_code_class_tag_, "2"}}, 1);
  expectCounter(cluster_store_, "prefix.upstream_rq_completed", "prefix.upstream_rq_completed", {},
                2);

  // The category is part of the stat name, and the code tags come along with it.
  expectCounter(cluster_store_, "prefix.canary.upstream_rq_200", "prefix.canary.upstream_rq",
                {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.external.upstream_rq_2xx", "prefix.external.upstream_rq_xx",
                {{response_code_class_tag_, "2"}}, 1);
  expectCounter(cluster_store_, "prefix.external.upstream_rq_completed",
                "prefix.external.upstream_rq_completed", {}, 1);

  // The zones are part of the stat name; they carry no tags of their own.
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_200",
                "prefix.zone.from_az.to_az.upstream_rq", {{response_code_tag_, "200"}}, 1);
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_completed",
                "prefix.zone.from_az.to_az.upstream_rq_completed", {}, 1);

  // An invalid response code holds no code to tag the stat with, and goes into no class.
  expectCounter(cluster_store_, "prefix.upstream_rq_unknown", "prefix.upstream_rq_unknown", {}, 1);
  expectCounter(cluster_store_, "prefix.internal.upstream_rq_unknown",
                "prefix.internal.upstream_rq_unknown", {}, 1);

  // The virtual host, the virtual cluster and the route are tags as well.
  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_200",
                "vhost.vcluster.upstream_rq",
                {{virtual_host_tag_, "test-vhost"},
                 {virtual_cluster_tag_, "test-cluster"},
                 {response_code_tag_, "200"}},
                1);
  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_completed",
                "vhost.vcluster.upstream_rq_completed",
                {{virtual_host_tag_, "test-vhost"}, {virtual_cluster_tag_, "test-cluster"}}, 1);
  expectCounter(global_store_, "vhost.test-vhost.route.test-route.upstream_rq_2xx",
                "vhost.route.upstream_rq_xx",
                {{virtual_host_tag_, "test-vhost"},
                 {route_tag_, "test-route"},
                 {response_code_class_tag_, "2"}},
                1);
}

// An invalid response code has no class to charge, so the vhost and zone stats fall back to the
// untagged 'upstream_rq_unknown' leaf rather than charging an empty class name.
TEST_P(TaggedCodeStatsTest, ExplicitTagsUnknownResponseCode) {
  addResponse(600, false, false, "test-vhost", "test-cluster", "from_az", "to_az", "test-route");

  expectCounter(global_store_, "vhost.test-vhost.vcluster.test-cluster.upstream_rq_unknown",
                "vhost.vcluster.upstream_rq_unknown",
                {{virtual_host_tag_, "test-vhost"}, {virtual_cluster_tag_, "test-cluster"}}, 1);
  expectCounter(global_store_, "vhost.test-vhost.route.test-route.upstream_rq_unknown",
                "vhost.route.upstream_rq_unknown",
                {{virtual_host_tag_, "test-vhost"}, {route_tag_, "test-route"}}, 1);
  expectCounter(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_unknown",
                "prefix.zone.from_az.to_az.upstream_rq_unknown", {}, 1);
}

TEST_P(TaggedCodeStatsTest, ResponseTimingTags) {
  Http::CodeStats::ResponseTimingInfo info{*global_store_.rootScope(),
                                           *cluster_store_.rootScope(),
                                           pool_.add("prefix"),
                                           std::chrono::milliseconds(5),
                                           true,
                                           true,
                                           pool_.add("vhost_name"),
                                           pool_.add("req_vcluster_name"),
                                           pool_.add("route_name"),
                                           pool_.add("from_az"),
                                           pool_.add("to_az")};

  code_stats_->chargeResponseTiming(info);

  const std::vector<uint64_t> five{5};
  expectHistogram(cluster_store_, "prefix.upstream_rq_time", "prefix.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.canary.upstream_rq_time",
                  "prefix.canary.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.internal.upstream_rq_time",
                  "prefix.internal.upstream_rq_time", {}, five);
  expectHistogram(cluster_store_, "prefix.zone.from_az.to_az.upstream_rq_time",
                  "prefix.zone.from_az.to_az.upstream_rq_time", {}, five);
  expectHistogram(global_store_, "vhost.vhost_name.vcluster.req_vcluster_name.upstream_rq_time",
                  "vhost.vcluster.upstream_rq_time",
                  {{virtual_host_tag_, "vhost_name"}, {virtual_cluster_tag_, "req_vcluster_name"}},
                  five);
  expectHistogram(global_store_, "vhost.vhost_name.route.route_name.upstream_rq_time",
                  "vhost.route.upstream_rq_time",
                  {{virtual_host_tag_, "vhost_name"}, {route_tag_, "route_name"}}, five);
}

// An isolated store cannot enumerate its histograms, so remember the name of every histogram a
// value is recorded on as it is delivered.
class RecordingTestStore : public Stats::TestUtil::TestStore {
public:
  using TestStore::TestStore;

  void deliverHistogramToSinks(const Stats::Histogram& histogram, uint64_t value) override {
    histogram_names_.insert(histogram.name());
    TestStore::deliverHistogramToSinks(histogram, value);
  }

  const std::set<std::string>& histogramNames() const { return histogram_names_; }

private:
  std::set<std::string> histogram_names_;
};

// Every stat one CodeStats implementation charged, by full name. The two implementations must
// produce the same maps; see the tests below.
struct ChargedStats {
  std::map<std::string, uint64_t> global_counters;
  std::map<std::string, uint64_t> cluster_counters;
  std::map<std::string, std::vector<uint64_t>> global_histograms;
  std::map<std::string, std::vector<uint64_t>> cluster_histograms;
};

// Response codes that fall inside 1xx-5xx: the edges of every class, plus a few common codes.
std::vector<uint64_t> validResponseCodes() {
  return {100, 199, 200, 201, 299, 300, 301, 399, 400, 404, 499, 500, 503, 599};
}

// Response codes that fall outside 1xx-5xx, and so belong to no class and have no name of their
// own. Note 600 is the first code past the end of the table of per-code names.
std::vector<uint64_t> unknownResponseCodes() { return {0, 23, 99, 600, 999, 1000000}; }

// Charges every combination of response code, canary, internal/external, stat prefix and naming
// context that a CodeStats implementation distinguishes, so that the stats the two of them name
// can be compared exhaustively rather than case by case.
template <class CodeStatsType>
void chargeMatrix(Stats::SymbolTable& symbol_table, Stats::TestUtil::TestStore& global_store,
                  Stats::TestUtil::TestStore& cluster_store,
                  const std::vector<uint64_t>& response_codes) {
  CodeStatsType code_stats(symbol_table);
  Stats::StatNamePool pool(symbol_table);

  const Stats::StatName empty;
  const Stats::StatName vhost = pool.add("test-vhost");
  const Stats::StatName vcluster = pool.add("test-cluster");
  const Stats::StatName route = pool.add("test-route");
  const Stats::StatName from_zone = pool.add("from_az");
  const Stats::StatName to_zone = pool.add("to_az");

  // Bit 0 selects the virtual cluster, bit 1 the route and bit 2 the zone pair. The virtual host
  // is named whenever either of the first two is, as the router always sets it alongside them.
  for (const uint32_t context : {0u, 1u, 2u, 4u, 7u}) {
    const Stats::StatName info_vhost = (context & 3) != 0 ? vhost : empty;
    const Stats::StatName info_vcluster = (context & 1) != 0 ? vcluster : empty;
    const Stats::StatName info_route = (context & 2) != 0 ? route : empty;
    const Stats::StatName info_from_zone = (context & 4) != 0 ? from_zone : empty;
    const Stats::StatName info_to_zone = (context & 4) != 0 ? to_zone : empty;

    // The router, ext_authz and ratelimit charge with an empty prefix; the alt-stat-prefix path
    // charges with a non-empty one.
    for (const absl::string_view prefix_string : {"prefix", ""}) {
      const Stats::StatName prefix = pool.add(prefix_string);
      for (const uint64_t response_code : response_codes) {
        for (const bool canary : {false, true}) {
          for (const bool internal : {false, true}) {
            for (const bool exclude_http_code_stats : {false, true}) {
              const Http::CodeStats::ResponseStatInfo info{*global_store.rootScope(),
                                                           *cluster_store.rootScope(),
                                                           prefix,
                                                           response_code,
                                                           internal,
                                                           info_vhost,
                                                           info_vcluster,
                                                           info_route,
                                                           info_from_zone,
                                                           info_to_zone,
                                                           canary};
              code_stats.chargeResponseStat(info, exclude_http_code_stats);
            }

            const Http::CodeStats::ResponseTimingInfo timing{*global_store.rootScope(),
                                                             *cluster_store.rootScope(),
                                                             prefix,
                                                             std::chrono::milliseconds(5),
                                                             canary,
                                                             internal,
                                                             info_vhost,
                                                             info_vcluster,
                                                             info_route,
                                                             info_from_zone,
                                                             info_to_zone};
            code_stats.chargeResponseTiming(timing);
          }
        }
      }
    }
  }
}

void collect(RecordingTestStore& store, std::map<std::string, uint64_t>& counters,
             std::map<std::string, std::vector<uint64_t>>& histograms) {
  for (const Stats::CounterSharedPtr& counter : store.counters()) {
    counters[counter->name()] = counter->value();
  }
  for (const std::string& name : store.histogramNames()) {
    histograms[name] = store.histogramValues(name, false);
  }
}

template <class CodeStatsType>
ChargedStats chargeAndCollect(const std::vector<uint64_t>& response_codes) {
  Stats::TestUtil::TestSymbolTable symbol_table;
  RecordingTestStore global_store(*symbol_table);
  RecordingTestStore cluster_store(*symbol_table);
  chargeMatrix<CodeStatsType>(*symbol_table, global_store, cluster_store, response_codes);

  ChargedStats charged;
  collect(global_store, charged.global_counters, charged.global_histograms);
  collect(cluster_store, charged.cluster_counters, charged.cluster_histograms);
  return charged;
}

// CodeStatsImpl and TaggedCodeStatsImpl must give every stat the same full name, so that a server
// switching between them renames nothing.
TEST(CodeStatsParityTest, SameStatNamesForValidResponseCodes) {
  const ChargedStats legacy = chargeAndCollect<CodeStatsImpl>(validResponseCodes());
  const ChargedStats tagged = chargeAndCollect<TaggedCodeStatsImpl>(validResponseCodes());

  EXPECT_FALSE(legacy.cluster_counters.empty());
  EXPECT_FALSE(legacy.global_counters.empty());
  EXPECT_EQ(legacy.cluster_counters, tagged.cluster_counters);
  EXPECT_EQ(legacy.global_counters, tagged.global_counters);
  EXPECT_EQ(legacy.cluster_histograms, tagged.cluster_histograms);
  EXPECT_EQ(legacy.global_histograms, tagged.global_histograms);
}

// Outside 1xx-5xx the two implementations agree on every stat but the leaf-less counters that
// CodeStatsImpl charges by joining an empty response code class onto the naming context; see the
// note above CodeStatsImpl::chargeResponseStat(). This pins that difference down so it stays
// deliberate, and confirms it is the only one.
TEST(CodeStatsParityTest, UnknownResponseCodesDifferOnlyByLeaflessCounters) {
  ChargedStats legacy = chargeAndCollect<CodeStatsImpl>(unknownResponseCodes());
  const ChargedStats tagged = chargeAndCollect<TaggedCodeStatsImpl>(unknownResponseCodes());

  for (const std::string name :
       {"vhost.test-vhost.vcluster.test-cluster", "vhost.test-vhost.route.test-route"}) {
    EXPECT_EQ(1U, legacy.global_counters.count(name)) << name;
    EXPECT_EQ(0U, tagged.global_counters.count(name)) << name;
    legacy.global_counters.erase(name);
  }
  for (const std::string name : {"prefix.zone.from_az.to_az", "zone.from_az.to_az"}) {
    EXPECT_EQ(1U, legacy.cluster_counters.count(name)) << name;
    EXPECT_EQ(0U, tagged.cluster_counters.count(name)) << name;
    legacy.cluster_counters.erase(name);
  }

  EXPECT_EQ(legacy.cluster_counters, tagged.cluster_counters);
  EXPECT_EQ(legacy.global_counters, tagged.global_counters);
  EXPECT_EQ(legacy.cluster_histograms, tagged.cluster_histograms);
  EXPECT_EQ(legacy.global_histograms, tagged.global_histograms);
}

// Every stat TaggedCodeStatsImpl charges must carry exactly the tags the default tag producer
// would have extracted from that stat's full name, and be tag-extracted to the same name, so that
// attaching the tags explicitly changes no stat's identity.
TEST(CodeStatsParityTest, TagsMatchTheDefaultTagProducer) {
  Stats::TestUtil::TestSymbolTable symbol_table;
  RecordingTestStore global_store(*symbol_table);
  RecordingTestStore cluster_store(*symbol_table);

  std::vector<uint64_t> response_codes = validResponseCodes();
  const std::vector<uint64_t> unknown = unknownResponseCodes();
  response_codes.insert(response_codes.end(), unknown.begin(), unknown.end());
  chargeMatrix<TaggedCodeStatsImpl>(*symbol_table, global_store, cluster_store, response_codes);

  const envoy::config::metrics::v3::StatsConfig stats_config;
  absl::StatusOr<Stats::TagProducerPtr> tag_producer =
      Stats::TagProducerImpl::createTagProducer(stats_config, {});
  ASSERT_TRUE(tag_producer.ok());

  uint32_t checked = 0;
  const auto check = [&tag_producer, &checked](const Stats::Metric& metric) {
    Stats::TagVector expected_tags;
    const std::string expected_name = (*tag_producer)->produceTags(metric.name(), expected_tags);
    EXPECT_EQ(expected_name, metric.tagExtractedName()) << " for stat '" << metric.name() << "'";
    EXPECT_EQ(sortedTags(expected_tags), sortedTags(metric.tags()))
        << " for stat '" << metric.name() << "'";
    ++checked;
  };

  for (RecordingTestStore* store : {&global_store, &cluster_store}) {
    for (const Stats::CounterSharedPtr& counter : store->counters()) {
      check(*counter);
    }
    for (const std::string& name : store->histogramNames()) {
      Stats::HistogramOptConstRef histogram = store->findHistogramByString(name);
      ASSERT_TRUE(histogram.has_value()) << "no histogram named '" << name << "'";
      check(histogram->get());
    }
  }
  EXPECT_LT(0U, checked);
}

} // namespace Http
} // namespace Envoy
