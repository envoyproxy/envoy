#include <regex>
#include <string>

#include "source/common/common/regex.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"
#include "source/server/admin/stats_request.h"

#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/real_threads_test_helper.h"
#include "test/test_common/stats_utility.h"
#include "test/test_common/utility.h"

using testing::Combine;
using testing::HasSubstr;
using testing::InSequence;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Values;
using testing::ValuesIn;

namespace Envoy {
namespace Server {

class StatsHandlerTest {
public:
  StatsHandlerTest() : pool_(symbol_table_), alloc_(symbol_table_) {
    store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_);
    store_->addSink(sink_);
    store_->initializeThreading(main_thread_dispatcher_, tls_);
  }

  ~StatsHandlerTest() {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  // Set buckets for tests.
  void setHistogramBucketSettings(const std::string& prefix, const std::vector<double>& buckets) {
    envoy::config::metrics::v3::StatsConfig config;
    auto& bucket_settings = *config.mutable_histogram_bucket_settings();

    envoy::config::metrics::v3::HistogramBucketSettings setting;
    setting.mutable_match()->set_prefix(prefix);
    setting.mutable_buckets()->Add(buckets.begin(), buckets.end());

    bucket_settings.Add(std::move(setting));
    store_->setHistogramSettings(std::make_unique<Stats::HistogramSettingsImpl>(config, context_));
  }

  using CodeResponse = std::pair<Http::Code, std::string>;

  /**
   * Issues a request for stat as json.
   *
   * @param used_only only include stats that have been written.
   * @param filter string interpreted as regex to filter stats
   * @return the Http Code and the response body as a string.
   */
  CodeResponse statsAsJsonHandler(const bool used_only, std::string filter = "") {
    std::string url = "stats?format=json";
    if (used_only) {
      url += "&usedonly";
    }
    absl::StrAppend(&url, filter);
    return handlerStats(url);
  }

  /**
   * Issues an admin request against the stats saved in store_.
   *
   * @param url the admin endpoint to query.
   * @return the Http Code and the response body as a string.
   */
  CodeResponse handlerStats(absl::string_view url) {
    NiceMock<MockInstance> instance;
    EXPECT_CALL(admin_stream_, getRequestHeaders()).WillRepeatedly(ReturnRef(request_headers_));
    EXPECT_CALL(instance, statsConfig()).WillRepeatedly(ReturnRef(stats_config_));
    EXPECT_CALL(stats_config_, flushOnAdmin()).WillRepeatedly(Return(false));
    ON_CALL(instance, stats()).WillByDefault(ReturnRef(*store_));
    ON_CALL(instance, clusterManager()).WillByDefault(ReturnRef(endpoints_helper_.cm_));
    EXPECT_CALL(instance, api()).WillRepeatedly(ReturnRef(api_));
    EXPECT_CALL(api_, customStatNamespaces()).WillRepeatedly(ReturnRef(custom_namespaces_));
    StatsHandler handler(instance);
    request_headers_.setPath(url);
    Admin::RequestPtr request = handler.makeRequest(admin_stream_);
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request->start(response_headers);
    Buffer::OwnedImpl data;
    while (request->nextChunk(data)) {
    }
    return std::make_pair(code, data.toString());
  }

  /**
   * Checks the presence and order of appearances of several fragments in a block of data.
   *
   * @param data the string to search
   * @param fragments ordered list of fragments to check.
   */
  void checkOrder(const absl::string_view& data, const std::vector<absl::string_view>& fragments) {
    size_t prev_pos = absl::string_view::npos;
    for (absl::string_view fragment : fragments) {
      size_t pos = data.find(fragment);
      EXPECT_NE(std::string::npos, pos) << fragment;
      if (prev_pos != absl::string_view::npos) {
        EXPECT_LT(prev_pos, pos) << fragment;
      }
      prev_pos = pos;
    }
  }

  Stats::StatName makeStat(absl::string_view name) { return pool_.add(name); }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  Upstream::PerEndpointMetricsTestHelper endpoints_helper_;
  Stats::AllocatorImpl alloc_;
  Stats::MockSink sink_;
  Stats::ThreadLocalStoreImplPtr store_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
  Http::TestRequestHeaderMapImpl request_headers_;
  MockAdminStream admin_stream_;
  Configuration::MockStatsConfig stats_config_;
};

class AdminStatsTest : public StatsHandlerTest, public testing::Test {};

TEST_F(AdminStatsTest, HandlerStatsInvalidFormat) {
  const std::string url = "/stats?format=blergh";
  const CodeResponse code_response(handlerStats(url));
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ("usage: /stats?format=(html|active-html|json|prometheus|text)\n\n",
            code_response.second);
}

TEST_F(AdminStatsTest, HandlerStatsPlainText) {
  const std::string url = "/stats";
  Buffer::OwnedImpl data, used_data;

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

  Stats::TextReadout& t = store_->textReadoutFromString("t");
  t.set("hello world");

  endpoints_helper_.makeCluster("mycluster", 1);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  constexpr char expected[] = "t: \"hello world\"\n"
                              "c1: 10\n"
                              "c2: 20\n"
                              "cluster.mycluster.endpoint.127.0.0.1_80.c1: 11\n"
                              "cluster.mycluster.endpoint.127.0.0.1_80.c2: 12\n"
                              "cluster.mycluster.endpoint.127.0.0.1_80.g1: 13\n"
                              "cluster.mycluster.endpoint.127.0.0.1_80.g2: 14\n"
                              "cluster.mycluster.endpoint.127.0.0.1_80.healthy: 1\n"
                              "h1: P0(200,200) P25(202.5,202.5) P50(205,205) P75(207.5,207.5) "
                              "P90(209,209) P95(209.5,209.5) P99(209.9,209.9) P99.5(209.95,209.95) "
                              "P99.9(209.99,209.99) P100(210,210)\n"
                              "h2: P0(100,100) P25(102.5,102.5) P50(105,105) P75(107.5,107.5) "
                              "P90(109,109) P95(109.5,109.5) P99(109.9,109.9) P99.5(109.95,109.95) "
                              "P99.9(109.99,109.99) P100(110,110)\n";
  EXPECT_EQ(expected, code_response.second);
}

#ifdef ENVOY_ADMIN_HTML
TEST_F(AdminStatsTest, HandlerStatsHtml) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  store_->rootScope()->counterFromStatName(makeStat("foo.c0")).add(0);
  Stats::ScopeSharedPtr scope0 = store_->createScope("");
  store_->rootScope()->counterFromStatName(makeStat("foo.c1")).add(1);
  Stats::ScopeSharedPtr scope = store_->createScope("scope");
  scope->gaugeFromStatName(makeStat("g2"), Stats::Gauge::ImportMode::Accumulate).set(2);
  Stats::ScopeSharedPtr scope2 = store_->createScope("scope1.scope2");
  scope2->textReadoutFromStatName(makeStat("t3")).set("text readout value");
  scope2->counterFromStatName(makeStat("unset"));

  auto test = [this](absl::string_view params, const std::vector<std::string>& expected,
                     const std::vector<std::string>& not_expected) {
    std::string url = absl::StrCat("/stats?format=html", params);
    CodeResponse code_response = handlerStats(url);
    EXPECT_EQ(Http::Code::OK, code_response.first);
    for (const std::string& expect : expected) {
      EXPECT_THAT(code_response.second, HasSubstr(expect)) << "params=" << params;
    }
    for (const std::string& not_expect : not_expected) {
      EXPECT_THAT(code_response.second, Not(HasSubstr(not_expect))) << "params=" << params;
    }
  };
  test("",
       {"foo.c0: 0", "foo.c1: 1", "scope.g2: 2", "scope1.scope2.unset: 0", // expected
        "scope1.scope2.t3: \"text readout value\"", "No Histograms found"},
       {"No TextReadouts found"});                   // not expected
  test("&type=Counters", {"foo.c0: 0", "foo.c1: 1"}, // expected
       {"No Histograms found", "scope.g2: 2"});      // not expected
  test("&usedonly", {"foo.c0: 0", "foo.c1: 1"},      // expected
       {"scope1.scope2.unset"});                     // not expected
}
#endif

TEST_F(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsCumulative) {
  const std::string url = "/stats?histogram_buckets=cumulative";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,0) "
            "B500(1,1) B1000(1,1) B2500(1,1) B5000(1,1) B10000(1,1) B30000(1,1) B60000(1,1) "
            "B300000(1,1) B600000(1,1) B1.8e+06(1,1) B3.6e+06(1,1)\n",
            code_response.second);
}

class AdminStatsFilterTest : public StatsHandlerTest, public testing::Test {
protected:
};

TEST_F(AdminStatsFilterTest, HandlerStatsPlainTextHistogramBucketsDisjoint) {
  const std::string url = "/stats?histogram_buckets=disjoint";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,1) "
            "B500(1,2) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\nh2: No recorded values\n",
            code_response.second);

  code_response = handlerStats(url + "&usedonly");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,1) "
            "B500(1,2) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\n",
            code_response.second);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 300));
  h2.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  code_response = handlerStats(url + "&usedonly&filter=h2");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ("h2: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,0) "
            "B500(1,1) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\n",
            code_response.second);
}

TEST_F(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsInvalid) {
  const std::string url = "/stats?histogram_buckets=invalid_input";
  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ("usage: /stats?histogram_buckets=(cumulative|disjoint|detailed|summary)\n",
            code_response.second);
}

TEST_F(AdminStatsTest, HandlerStatsJsonNoHistograms) {
  const std::string url = "/stats?format=json&usedonly";

  Stats::Counter& c1 = store_->counterFromString("c1");

  c1.add(10);

  store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_json = R"EOF({"stats": [{"name":"c1", "value":10}]})EOF";

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_THAT(expected_json, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&histogram_buckets=cumulative");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_THAT(expected_json, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&histogram_buckets=disjoint");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_THAT(expected_json, JsonStringEq(code_response.second));
}

TEST_F(AdminStatsFilterTest, HandlerStatsJsonHistogramBucketsCumulative) {
  const std::string url = "/stats?histogram_buckets=cumulative&format=json";
  // Set h as prefix to match both histograms.
  setHistogramBucketSettings("h", {1, 2, 3, 4});

  Stats::Counter& c1 = store_->counterFromString("c1");

  c1.add(10);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 2));
  h1.recordValue(2);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 2));
  h1.recordValue(2);

  store_->mergeHistograms([]() -> void {});

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);

  const std::string expected_json = R"EOF({
    "stats": [
        {"name":"c1", "value":10},
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":3},
                        {"upper_bound":4, "interval":1, "cumulative":3}
                    ]
                },
                {
                    "name": "h2", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":0},
                        {"upper_bound":3, "interval":0, "cumulative":0},
                        {"upper_bound":4, "interval":0, "cumulative":0}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&usedonly");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_used = R"EOF({
    "stats": [
        {"name":"c1", "value":10},
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":3},
                        {"upper_bound":4, "interval":1, "cumulative":3}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&usedonly&filter=h1");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_used_and_filter = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":3},
                        {"upper_bound":4, "interval":1, "cumulative":3}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used_and_filter, JsonStringEq(code_response.second));
}

TEST_F(AdminStatsFilterTest, HandlerStatsJsonHiddenGauges) {
  // Test that hidden=include shows all hidden and non hidden values.
  const std::string url_hidden_include = "/stats?gauges&format=json&hidden=include";

  Stats::Gauge& g1 =
      store_->gaugeFromString("hiddenG1", Stats::Gauge::ImportMode::HiddenAccumulate);
  g1.inc();

  Stats::Gauge& g2 = store_->gaugeFromString("nonHiddenG2", Stats::Gauge::ImportMode::Accumulate);
  g2.add(2);

  CodeResponse code_response = handlerStats(url_hidden_include);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_hidden_include = R"EOF({
    "stats": [
        {"name":"hiddenG1", "value":1},
        {"name":"nonHiddenG2", "value":2},
    ]
})EOF";
  EXPECT_THAT(expected_json_hidden_include, JsonStringEq(code_response.second));

  // Test that hidden-only will not show non-hidden gauges.
  const std::string url_hidden_show_only = "/stats?gauges&format=json&hidden=only";

  code_response = handlerStats(url_hidden_show_only);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_hidden_show_only = R"EOF({
    "stats": [
        {"name":"hiddenG1", "value":1},
    ]
})EOF";
  EXPECT_THAT(expected_json_hidden_show_only, JsonStringEq(code_response.second));

  // Test that hidden=exclude will not show hidden gauges.
  const std::string url_hidden_exclude = "/stats?gauges&format=json&hidden=exclude";

  code_response = handlerStats(url_hidden_exclude);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_hidden_exclude = R"EOF({
    "stats": [
        {"name":"nonHiddenG2", "value":2},
    ]
})EOF";

  EXPECT_THAT(expected_json_hidden_exclude, JsonStringEq(code_response.second));
}

TEST_F(AdminStatsFilterTest, HandlerStatsHiddenInvalid) {
  // Test that hidden=(bad inputs) returns error.
  const std::string url_hidden_bad_input = "/stats?gauges&format=json&hidden=foo";

  CodeResponse code_response = handlerStats(url_hidden_bad_input);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ("usage: /stats?hidden=(include|only|exclude)\n\n", code_response.second);
}

TEST_F(AdminStatsFilterTest, HandlerStatsJsonHistogramBucketsDisjoint) {
  const std::string url = "/stats?histogram_buckets=disjoint&format=json";
  // Set h as prefix to match both histograms.
  setHistogramBucketSettings("h", {1, 2, 3, 4});

  Stats::Counter& c1 = store_->counterFromString("c1");

  c1.add(10);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 1));
  h1.recordValue(1);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 2));
  h1.recordValue(2);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 2));
  h1.recordValue(2);

  store_->mergeHistograms([]() -> void {});

  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);

  const std::string expected_json = R"EOF({
    "stats": [
        {"name":"c1", "value":10},
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":2},
                        {"upper_bound":4, "interval":0, "cumulative":0}
                    ]
                },
                {
                    "name": "h2", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":0},
                        {"upper_bound":3, "interval":0, "cumulative":0},
                        {"upper_bound":4, "interval":0, "cumulative":0}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&usedonly");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_used = R"EOF({
    "stats": [
        {"name":"c1", "value":10},
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":2},
                        {"upper_bound":4, "interval":0, "cumulative":0}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used, JsonStringEq(code_response.second));

  code_response = handlerStats(url + "&usedonly&filter=h1");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  const std::string expected_json_used_and_filter = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1", "buckets": [
                        {"upper_bound":1, "interval":0, "cumulative":0},
                        {"upper_bound":2, "interval":0, "cumulative":1},
                        {"upper_bound":3, "interval":1, "cumulative":2},
                        {"upper_bound":4, "interval":0, "cumulative":0}
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used_and_filter, JsonStringEq(code_response.second));
}

TEST_F(AdminStatsTest, HandlerStatsJson) {
  const std::string url = "/stats?format=json";

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

  endpoints_helper_.makeCluster("mycluster", 1);

  Stats::TextReadout& t = store_->textReadoutFromString("t");
  t.set("hello world");

  Stats::Histogram& h = store_->histogramFromString("h", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h), 200));
  h.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);

  const std::string expected_json_old = R"EOF({
    "stats": [
        {
            "name":"t",
            "value":"hello world"
        },
        {
            "name":"c1",
            "value":10
        },
        {
            "name":"c2",
            "value":20
        },
        {
           "name":"cluster.mycluster.endpoint.127.0.0.1_80.c1",
           "value":11
        },
        {
           "name":"cluster.mycluster.endpoint.127.0.0.1_80.c2",
           "value":12
        },
        {
           "name":"cluster.mycluster.endpoint.127.0.0.1_80.g1",
           "value":13
        },
        {
           "name":"cluster.mycluster.endpoint.127.0.0.1_80.g2",
           "value":14
        },
        {
           "name":"cluster.mycluster.endpoint.127.0.0.1_80.healthy",
           "value":1
        },
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name":"h",
                        "values": [
                            {
                                "cumulative":200,
                                "interval":200
                            },
                            {
                                "cumulative":202.5,
                                "interval":202.5
                            },
                            {
                                "cumulative":205,
                                "interval":205
                            },
                            {
                                "cumulative":207.5,
                                "interval":207.5
                            },
                            {
                                "cumulative":209,
                                "interval":209
                            },
                            {
                                "cumulative":209.5,
                                "interval":209.5
                            },
                            {
                                "cumulative":209.9,
                                "interval":209.9
                            },
                            {
                                "cumulative":209.95,
                                "interval":209.95
                            },
                            {
                                "cumulative":209.99,
                                "interval":209.99
                            },
                            {
                                "cumulative":210,
                                "interval":210
                            }
                        ]
                    },
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_old, JsonStringEq(code_response.second));
}

TEST_F(AdminStatsTest, StatsAsJson) {
  InSequence s;

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  const std::string actual_json = statsAsJsonHandler(false).second;

  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    },
                    {
                        "name": "h2",
                        "values": [
                            {
                                "interval": null,
                                "cumulative": 100.0
                            },
                            {
                                "interval": null,
                                "cumulative": 102.5
                            },
                            {
                                "interval": null,
                                "cumulative": 105.0
                            },
                            {
                                "interval": null,
                                "cumulative": 107.5
                            },
                            {
                                "interval": null,
                                "cumulative": 109.0
                            },
                            {
                                "interval": null,
                                "cumulative": 109.5
                            },
                            {
                                "interval": null,
                                "cumulative": 109.9
                            },
                            {
                                "interval": null,
                                "cumulative": 109.95
                            },
                            {
                                "interval": null,
                                "cumulative": 109.99
                            },
                            {
                                "interval": null,
                                "cumulative": 110.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
}

TEST_F(AdminStatsTest, UsedOnlyStatsAsJson) {
  InSequence s;

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("h2", h2.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  const std::string actual_json = statsAsJsonHandler(true).second;

  // Expected JSON should not have h2 values as it is not used.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
}

TEST_F(AdminStatsFilterTest, StatsAsJsonFilterString) {
  InSequence s;

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 so that it has both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  const std::string actual_json = statsAsJsonHandler(false, "&filter=[a-z]1").second;

  // Because this is a filter case, we don't expect to see any stats except for those containing
  // "h1" in their name.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
}

TEST_F(AdminStatsFilterTest, UsedOnlyStatsAsJsonFilterString) {
  InSequence s;

  Stats::Histogram& h1 = store_->histogramFromString(
      "h1_matches", Stats::Histogram::Unit::Unspecified); // Will match, be used, and print
  Stats::Histogram& h2 = store_->histogramFromString(
      "h2_matches", Stats::Histogram::Unit::Unspecified); // Will match but not be used
  Stats::Histogram& h3 = store_->histogramFromString(
      "h3_not", Stats::Histogram::Unit::Unspecified); // Will be used but not match

  EXPECT_EQ("h1_matches", h1.name());
  EXPECT_EQ("h2_matches", h2.name());
  EXPECT_EQ("h3_not", h3.name());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h3), 200));
  h3.recordValue(200);

  store_->mergeHistograms([]() -> void {});

  // Again record a new value in h1 and h3 so that they have both interval and cumulative values.
  // h2 should only have cumulative values.
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h3), 100));
  h3.recordValue(100);

  store_->mergeHistograms([]() -> void {});
  const std::string actual_json = statsAsJsonHandler(true, "&filter=h[12]&safe").second;

  // Expected JSON should not have h2 values as it is not used, and should not have h3 values as
  // they are used but do not match.
  const std::string expected_json = R"EOF({
    "stats": [
        {
            "histograms": {
                "supported_quantiles": [
                    0.0,
                    25.0,
                    50.0,
                    75.0,
                    90.0,
                    95.0,
                    99.0,
                    99.5,
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
                    {
                        "name": "h1_matches",
                        "values": [
                            {
                                "interval": 100.0,
                                "cumulative": 100.0
                            },
                            {
                                "interval": 102.5,
                                "cumulative": 105.0
                            },
                            {
                                "interval": 105.0,
                                "cumulative": 110.0
                            },
                            {
                                "interval": 107.5,
                                "cumulative": 205.0
                            },
                            {
                                "interval": 109.0,
                                "cumulative": 208.0
                            },
                            {
                                "interval": 109.5,
                                "cumulative": 209.0
                            },
                            {
                                "interval": 109.9,
                                "cumulative": 209.8
                            },
                            {
                                "interval": 109.95,
                                "cumulative": 209.9
                            },
                            {
                                "interval": 109.99,
                                "cumulative": 209.98
                            },
                            {
                                "interval": 110.0,
                                "cumulative": 210.0
                            }
                        ]
                    }
                ]
            }
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(actual_json));
}

TEST_F(AdminStatsTest, SortedCountersAndGauges) {
  // Check counters and gauges are co-mingled in sorted order in the admin output.
  store_->gaugeFromString("s4", Stats::Gauge::ImportMode::Accumulate);
  store_->counterFromString("s3");
  store_->counterFromString("s1");
  store_->gaugeFromString("s2", Stats::Gauge::ImportMode::Accumulate);
  for (absl::string_view url : {"/stats", "/stats?format=json"}) {
    const CodeResponse code_response = handlerStats(url);
    ASSERT_EQ(Http::Code::OK, code_response.first);
    checkOrder(code_response.second, {"s1", "s2", "s3", "s4"});
  }
}

TEST_F(AdminStatsTest, SortedScopes) {
  // Check counters and gauges are co-mingled in sorted order in the admin output.
  store_->counterFromString("a");
  store_->counterFromString("z");
  Stats::ScopeSharedPtr scope = store_->createScope("scope");
  scope->counterFromString("r");
  scope->counterFromString("s");
  scope->counterFromString("t");
  Stats::ScopeSharedPtr subscope = scope->createScope("subscope");
  subscope->counterFromString("x");
  for (absl::string_view url : {"/stats", "/stats?format=json"}) {
    CodeResponse code_response = handlerStats(url);
    ASSERT_EQ(Http::Code::OK, code_response.first);
    checkOrder(code_response.second,
               {"a", "scope.r", "scope.s", "scope.subscope.x", "scope.t", "z"});
  }
}

TEST_F(AdminStatsTest, SortedTextReadouts) {
  // Check counters and gauges are co-mingled in sorted order in the admin output.
  store_->textReadoutFromString("t4");
  store_->textReadoutFromString("t3");
  store_->textReadoutFromString("t1");
  store_->textReadoutFromString("t2");
  for (absl::string_view url : {"/stats", "/stats?format=json"}) {
    const CodeResponse code_response = handlerStats(url);
    ASSERT_EQ(Http::Code::OK, code_response.first);
    checkOrder(code_response.second, {"t1", "t2", "t3", "t4"});
  }
}

TEST_F(AdminStatsTest, SortedHistograms) {
  store_->histogramFromString("h4", Stats::Histogram::Unit::Unspecified);
  store_->histogramFromString("h3", Stats::Histogram::Unit::Unspecified);
  store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  for (absl::string_view url : {"/stats", "/stats?format=json"}) {
    const CodeResponse code_response = handlerStats(url);
    ASSERT_EQ(Http::Code::OK, code_response.first);
    checkOrder(code_response.second, {"h1", "h2", "h3", "h4"});
  }
}

// Sets up a test using real threads to reproduce a race between deleting scopes
// and iterating over them.
class ThreadedTest : public testing::Test {
protected:
  // These values were picked by trial and error, with a log added to
  // ThreadLocalStoreImpl::iterateScopesLockHeld for when locked==nullptr. The
  // goal with these numbers was to maximize the number of times we'd have to
  // skip over a deleted scope that fails the lock, while keeping the test
  // duration under a few seconds. On one dev box, these settings allow for
  // about 20 reproductions of the race in a 5 second test.
  static constexpr uint32_t NumThreads = 12;
  static constexpr uint32_t NumScopes = 5;
  static constexpr uint32_t NumStatsPerScope = 5;
  static constexpr uint32_t NumIters = 20;

  ThreadedTest()
      : real_threads_(NumThreads), pool_(symbol_table_), alloc_(symbol_table_),
        store_(std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_)) {
    for (uint32_t i = 0; i < NumStatsPerScope; ++i) {
      counter_names_[i] = pool_.add(absl::StrCat("c", "_", i));
    }
    for (uint32_t i = 0; i < NumScopes; ++i) {
      scope_names_[i] = pool_.add(absl::StrCat("scope", "_", i));
    }
    real_threads_.runOnMainBlocking([this]() {
      store_->initializeThreading(real_threads_.mainDispatcher(), real_threads_.tls());
    });
  }

  ~ThreadedTest() override {
    real_threads_.runOnMainBlocking([this]() {
      ThreadLocal::Instance& tls = real_threads_.tls();
      if (!tls.isShutdown()) {
        tls.shutdownGlobalThreading();
      }
      store_->shutdownThreading();
      tls.shutdownThread();
    });
    real_threads_.exitThreads([this]() {
      scopes_.clear();
      store_.reset();
    });
  }

  // Builds, or re-builds, NumScopes scopes, each of which has NumStatsPerScopes
  // counters. The scopes are kept in an already-sized vector. We keep a
  // fine-grained mutex for each scope just for each entry in the scope vector
  // so we can have multiple threads concurrently rebuilding the scopes.
  void addStats() {
    ASSERT_EQ(NumScopes, scopes_.size());
    for (uint32_t s = 0; s < NumScopes; ++s) {
      Stats::ScopeSharedPtr scope = store_->rootScope()->scopeFromStatName(scope_names_[s]);
      {
        absl::MutexLock lock(&scope_mutexes_[s]);
        scopes_[s] = scope;
      }
      for (Stats::StatName counter_name : counter_names_) {
        scope->counterFromStatName(counter_name);
      }
    }
  }

  void statsEndpoint() {
    StatsRequest request(*store_, StatsParams(), cm_);
    Http::TestResponseHeaderMapImpl response_headers;
    request.start(response_headers);
    Buffer::OwnedImpl data;
    while (request.nextChunk(data)) {
    }
    for (const Buffer::RawSlice& slice : data.getRawSlices()) {
      absl::string_view str(static_cast<const char*>(slice.mem_), slice.len_);
      // Sanity check that the /stats endpoint is doing something by counting
      // newlines.
      total_lines_ += std::count_if(str.begin(), str.end(), [](char c) { return c == '\n'; });
    }
  }

  Thread::RealThreadsTestHelper real_threads_;
  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl alloc_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> store_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::vector<Stats::ScopeSharedPtr> scopes_{NumScopes};
  absl::Mutex scope_mutexes_[NumScopes];
  std::atomic<uint64_t> total_lines_{0};
  Stats::StatName counter_names_[NumStatsPerScope];
  Stats::StatName scope_names_[NumScopes];
};

TEST_F(ThreadedTest, Threaded) {
  real_threads_.runOnAllWorkersBlocking([this]() {
    for (uint32_t i = 0; i < NumIters; ++i) {
      addStats();
      statsEndpoint();
    }
  });

  // We expect all the constants, multiplied together, give us the expected
  // number of lines. However, whenever there is an attempt to iterate over
  // scopes in one thread while another thread has replaced that scope, we will
  // drop some scopes and/or stats in an in-construction scope. So we just test
  // here that the number of lines is between 0.5x and 1.x expected. If this
  // proves flaky we can loosen this check.
  uint32_t expected = NumThreads * NumScopes * NumStatsPerScope * NumIters;
  EXPECT_GE(expected, total_lines_);
  EXPECT_LE(expected / 2, total_lines_);
}

TEST_F(AdminStatsFilterTest, StatsInvalidRegex) {
  for (absl::string_view path :
       {"/stats?filter=*.test", "/stats?format=prometheus&filter=*.test"}) {
    CodeResponse code_response;
    code_response = handlerStats(path);
    EXPECT_EQ("Invalid re2 regex", code_response.second) << path;
    EXPECT_EQ(Http::Code::BadRequest, code_response.first) << path;
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdminInstanceTest, TracingStatsDisabled) {
  const std::string& name = admin_.tracingStats().service_forced_.name();
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    EXPECT_NE(counter->name(), name) << "Unexpected tracing stat found in server stats: " << name;
  }
}

TEST_P(AdminInstanceTest, GetRequestJson) {
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats?format=json", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("{\"stats\":["));
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("application/json"));
}

TEST_P(AdminInstanceTest, RecentLookups) {
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;

  // Recent lookup tracking is disabled by default.
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats/recentlookups", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("Lookup tracking is not enabled"));
  EXPECT_THAT(std::string(response_headers.getContentTypeValue()), HasSubstr("text/plain"));

  EXPECT_EQ(Http::Code::OK,
            admin_.request("/stats/recentlookups/enable", "POST", response_headers, body));
  Stats::StatNamePool pool(server_.stats().symbolTable());
  pool.add("alpha");
  pool.add("beta");
  pool.add("gamma");
  pool.add("alpha");
  pool.add("beta");
  pool.add("alpha");
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats/recentlookups", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("       1 gamma\n       2 beta\n       3 alpha\n"));
}

class StatsHandlerPrometheusTest : public StatsHandlerTest {
public:
  void createTestStats() {
    Stats::StatNameTagVector c1Tags{{makeStat("cluster"), makeStat("c1")}};
    Stats::StatNameTagVector c2Tags{{makeStat("cluster"), makeStat("c2")}};

    Stats::Counter& c1 = store_->rootScope()->counterFromStatNameWithTags(
        makeStat("cluster.upstream.cx.total"), c1Tags);
    c1.add(10);
    Stats::Counter& c2 = store_->rootScope()->counterFromStatNameWithTags(
        makeStat("cluster.upstream.cx.total"), c2Tags);
    c2.add(20);

    Stats::Gauge& g1 = store_->rootScope()->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c1Tags, Stats::Gauge::ImportMode::Accumulate);
    g1.set(11);
    Stats::Gauge& g2 = store_->rootScope()->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c2Tags, Stats::Gauge::ImportMode::Accumulate);
    g2.set(12);

    Stats::TextReadout& t1 = store_->rootScope()->textReadoutFromStatNameWithTags(
        makeStat("control_plane.identifier"), c1Tags);
    t1.set("cp-1");
  }
};

class StatsHandlerPrometheusDefaultTest : public StatsHandlerPrometheusTest, public testing::Test {
public:
};

TEST_F(StatsHandlerPrometheusDefaultTest, StatsHandlerPrometheusDefaultTest) {
  const std::string url = "/stats?format=prometheus";

  createTestStats();

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20
# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12
)EOF";

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ(expected_response, code_response.second);
}

TEST_F(StatsHandlerPrometheusDefaultTest, StatsHandlerPrometheusInvalidRegex) {
  const std::string url = "/stats?format=prometheus&filter=(+invalid)";

  createTestStats();

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_THAT(code_response.second, HasSubstr("Invalid re2 regex"));
}

TEST_F(StatsHandlerPrometheusDefaultTest, HandlerStatsPrometheusDefaultHistogramEmission) {
  const std::string url = "/stats?format=prometheus";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_response = R"EOF(# TYPE envoy_h1 histogram
envoy_h1_bucket{le="0.5"} 0
envoy_h1_bucket{le="1"} 0
envoy_h1_bucket{le="5"} 0
envoy_h1_bucket{le="10"} 0
envoy_h1_bucket{le="25"} 0
envoy_h1_bucket{le="50"} 0
envoy_h1_bucket{le="100"} 0
envoy_h1_bucket{le="250"} 0
envoy_h1_bucket{le="500"} 1
envoy_h1_bucket{le="1000"} 1
envoy_h1_bucket{le="2500"} 1
envoy_h1_bucket{le="5000"} 1
envoy_h1_bucket{le="10000"} 1
envoy_h1_bucket{le="30000"} 1
envoy_h1_bucket{le="60000"} 1
envoy_h1_bucket{le="300000"} 1
envoy_h1_bucket{le="600000"} 1
envoy_h1_bucket{le="1800000"} 1
envoy_h1_bucket{le="3600000"} 1
envoy_h1_bucket{le="+Inf"} 1
envoy_h1_sum{} 305
envoy_h1_count{} 1
)EOF";

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ(expected_response, code_response.second);
}

TEST_F(StatsHandlerPrometheusDefaultTest, HandlerStatsPrometheusExplicitHistogramEmission) {
  const std::string url = "/stats?format=prometheus&histogram_buckets=cumulative";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_response = R"EOF(# TYPE envoy_h1 histogram
envoy_h1_bucket{le="0.5"} 0
envoy_h1_bucket{le="1"} 0
envoy_h1_bucket{le="5"} 0
envoy_h1_bucket{le="10"} 0
envoy_h1_bucket{le="25"} 0
envoy_h1_bucket{le="50"} 0
envoy_h1_bucket{le="100"} 0
envoy_h1_bucket{le="250"} 0
envoy_h1_bucket{le="500"} 1
envoy_h1_bucket{le="1000"} 1
envoy_h1_bucket{le="2500"} 1
envoy_h1_bucket{le="5000"} 1
envoy_h1_bucket{le="10000"} 1
envoy_h1_bucket{le="30000"} 1
envoy_h1_bucket{le="60000"} 1
envoy_h1_bucket{le="300000"} 1
envoy_h1_bucket{le="600000"} 1
envoy_h1_bucket{le="1800000"} 1
envoy_h1_bucket{le="3600000"} 1
envoy_h1_bucket{le="+Inf"} 1
envoy_h1_sum{} 305
envoy_h1_count{} 1
)EOF";

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ(expected_response, code_response.second);
}

TEST_F(StatsHandlerPrometheusDefaultTest, HandlerStatsPrometheusSummaryEmission) {
  const std::string url = "/stats?format=prometheus&histogram_buckets=summary";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_response = R"EOF(# TYPE envoy_h1 summary
envoy_h1{quantile="0"} 300
envoy_h1{quantile="0.25"} 302.5
envoy_h1{quantile="0.5"} 305
envoy_h1{quantile="0.75"} 307.5
envoy_h1{quantile="0.9"} 309
envoy_h1{quantile="0.95"} 309.5
envoy_h1{quantile="0.99"} 309.89999999999997726263245567679
envoy_h1{quantile="0.995"} 309.9499999999999886313162278384
envoy_h1{quantile="0.999"} 309.99000000000000909494701772928
envoy_h1{quantile="1"} 310
envoy_h1_sum{} 305
envoy_h1_count{} 1
)EOF";

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ(expected_response, code_response.second);
}

TEST_F(StatsHandlerPrometheusDefaultTest, HandlerStatsPrometheusUnsupportedBucketMode) {
  const std::string url = "/stats?format=prometheus&histogram_buckets=disjoint";

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_response = "unsupported prometheus histogram bucket mode";
  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ(expected_response, code_response.second);
}

class StatsHandlerPrometheusWithTextReadoutsTest
    : public StatsHandlerPrometheusTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, std::string>> {};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndUrls, StatsHandlerPrometheusWithTextReadoutsTest,
                         Combine(ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                 Values("/stats?format=prometheus&text_readouts",
                                        "/stats?format=prometheus&text_readouts=true",
                                        "/stats?format=prometheus&text_readouts=false",
                                        "/stats?format=prometheus&text_readouts=abc")));

TEST_P(StatsHandlerPrometheusWithTextReadoutsTest, StatsHandlerPrometheusWithTextReadoutsTest) {
  const std::string url = std::get<1>(GetParam());

  createTestStats();

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20
# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12
# TYPE envoy_control_plane_identifier gauge
envoy_control_plane_identifier{cluster="c1",text_value="cp-1"} 0
)EOF";

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_THAT(expected_response, code_response.second);
}

} // namespace Server
} // namespace Envoy
