#include <regex>
#include <string>

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_handler.h"

#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

using testing::EndsWith;
using testing::HasSubstr;
using testing::InSequence;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::StartsWith;

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
    store_->setHistogramSettings(std::make_unique<Stats::HistogramSettingsImpl>(config));
  }

  using CodeResponse = std::pair<Http::Code, std::string>;

  /**
   * Issues a request for stat as json.
   *
   * @param used_only only include stats that have been written.
   * @param filter string interpreted as regex to filter stats
   * @return the Http Code and the response body as a string.
   */
  CodeResponse statsAsJsonHandler(const bool used_only,
                                  absl::optional<std::string> filter = absl::nullopt) {

    std::string url = "stats?format=json";
    if (used_only) {
      url += "&usedonly";
    }
    if (filter.has_value()) {
      absl::StrAppend(&url, "&filter=", filter.value());
    }
    return handlerStats(url);
  }

  /**
   * Issues an admin request against the stats saved in store_.
   *
   * @param url the admin endpoint to query.
   * @return the Http Code and the response body as a string.
   */
  CodeResponse handlerStats(absl::string_view url) {
    MockInstance instance;
    EXPECT_CALL(instance, statsConfig()).WillRepeatedly(ReturnRef(stats_config_));
    EXPECT_CALL(stats_config_, flushOnAdmin()).WillRepeatedly(Return(false));
    EXPECT_CALL(instance, stats()).WillRepeatedly(ReturnRef(*store_));
    EXPECT_CALL(instance, api()).WillRepeatedly(ReturnRef(api_));
    EXPECT_CALL(api_, customStatNamespaces()).WillRepeatedly(ReturnRef(custom_namespaces_));
    StatsHandler handler(instance);
    Buffer::OwnedImpl data;
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream_);
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

  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  Stats::AllocatorImpl alloc_;
  Stats::MockSink sink_;
  Stats::ThreadLocalStoreImplPtr store_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
  MockAdminStream admin_stream_;
  Configuration::MockStatsConfig stats_config_;
};

class AdminStatsTest : public StatsHandlerTest,
                       public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminStatsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminStatsTest, HandlerStatsInvalidFormat) {
  const std::string url = "/stats?format=blergh";
  const CodeResponse code_response(handlerStats(url));
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ("usage: /stats?format=json  or /stats?format=prometheus \n\n", code_response.second);
}

TEST_P(AdminStatsTest, HandlerStatsPlainText) {
  const std::string url = "/stats";

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

  Stats::TextReadout& t = store_->textReadoutFromString("t");
  t.set("hello world");

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
                              "h1: P0(200,200) P25(202.5,202.5) P50(205,205) P75(207.5,207.5) "
                              "P90(209,209) P95(209.5,209.5) P99(209.9,209.9) P99.5(209.95,209.95) "
                              "P99.9(209.99,209.99) P100(210,210)\n"
                              "h2: P0(100,100) P25(102.5,102.5) P50(105,105) P75(107.5,107.5) "
                              "P90(109,109) P95(109.5,109.5) P99(109.9,109.9) P99.5(109.95,109.95) "
                              "P99.9(109.99,109.99) P100(110,110)\n";
  EXPECT_EQ(expected, code_response.second);

  code_response = handlerStats(url + "?usedonly");
  EXPECT_EQ(Http::Code::OK, code_response.first);
  EXPECT_EQ(expected, code_response.second);
}

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsCumulative) {
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

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsDisjoint) {
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

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsInvalid) {
  const std::string url = "/stats?histogram_buckets=invalid_input";
  CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_EQ("usage: /stats?histogram_buckets=cumulative  or /stats?histogram_buckets=disjoint \n",
            code_response.second);
}

TEST_P(AdminStatsTest, HandlerStatsJsonNoHistograms) {
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

TEST_P(AdminStatsTest, HandlerStatsJsonHistogramBucketsCumulative) {
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

TEST_P(AdminStatsTest, HandlerStatsJsonHistogramBucketsDisjoint) {
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

TEST_P(AdminStatsTest, HandlerStatsJson) {
  const std::string url = "/stats?format=json";

  Stats::Counter& c1 = store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c1.add(10);
  c2.add(20);

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
            "value":10,
        },
        {
            "name":"c2",
            "value":20
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

TEST_P(AdminStatsTest, StatsAsJson) {
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

TEST_P(AdminStatsTest, UsedOnlyStatsAsJson) {
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

TEST_P(AdminStatsTest, StatsAsJsonFilterString) {
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
  const std::string actual_json = statsAsJsonHandler(false, "[a-z]1").second;

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

TEST_P(AdminStatsTest, UsedOnlyStatsAsJsonFilterString) {
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
  const std::string actual_json = statsAsJsonHandler(true, "h[12]").second;

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

TEST_P(AdminStatsTest, SortedCountersAndGauges) {
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

TEST_P(AdminStatsTest, SortedScopes) {
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

TEST_P(AdminStatsTest, SortedTextReadouts) {
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

TEST_P(AdminStatsTest, SortedHistograms) {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, StatsInvalidRegex) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl data;
  EXPECT_LOG_CONTAINS(
      "error", "Invalid regex: ",
      EXPECT_EQ(Http::Code::BadRequest, getCallback("/stats?filter=*.test", header_map, data)));

  // Note: depending on the library, the detailed error message might be one of:
  //   "One of *?+{ was not preceded by a valid regular expression."
  //   "regex_error"
  // but we always precede by 'Invalid regex: "'.
  EXPECT_THAT(data.toString(), StartsWith("Invalid regex: \""));
  EXPECT_THAT(data.toString(), EndsWith("\"\n"));
}

TEST_P(AdminInstanceTest, PrometheusStatsInvalidRegex) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl data;
  EXPECT_LOG_CONTAINS(
      "error", ": *.ptest",
      EXPECT_EQ(Http::Code::BadRequest,
                getCallback("/stats?format=prometheus&filter=*.ptest", header_map, data)));

  // Note: depending on the library, the detailed error message might be one of:
  //   "One of *?+{ was not preceded by a valid regular expression."
  //   "regex_error"
  // but we always precede by 'Invalid regex: "'.
  EXPECT_THAT(data.toString(), StartsWith("Invalid regex: \""));
  EXPECT_THAT(data.toString(), EndsWith("\"\n"));
}

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

    Stats::Counter& c1 =
        store_->counterFromStatNameWithTags(makeStat("cluster.upstream.cx.total"), c1Tags);
    c1.add(10);
    Stats::Counter& c2 =
        store_->counterFromStatNameWithTags(makeStat("cluster.upstream.cx.total"), c2Tags);
    c2.add(20);

    Stats::Gauge& g1 = store_->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c1Tags, Stats::Gauge::ImportMode::Accumulate);
    g1.set(11);
    Stats::Gauge& g2 = store_->gaugeFromStatNameWithTags(
        makeStat("cluster.upstream.cx.active"), c2Tags, Stats::Gauge::ImportMode::Accumulate);
    g2.set(12);

    Stats::TextReadout& t1 =
        store_->textReadoutFromStatNameWithTags(makeStat("control_plane.identifier"), c1Tags);
    t1.set("cp-1");
  }
};

class StatsHandlerPrometheusDefaultTest
    : public StatsHandlerPrometheusTest,
      public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsHandlerPrometheusDefaultTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsHandlerPrometheusDefaultTest, StatsHandlerPrometheusDefaultTest) {
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

TEST_P(StatsHandlerPrometheusDefaultTest, StatsHandlerPrometheusInvalidRegex) {
  const std::string url = "/stats?format=prometheus&filter=(+invalid)";

  createTestStats();

  const CodeResponse code_response = handlerStats(url);
  EXPECT_EQ(Http::Code::BadRequest, code_response.first);
  EXPECT_THAT(code_response.second, HasSubstr("Invalid regex"));
}

class StatsHandlerPrometheusWithTextReadoutsTest
    : public StatsHandlerPrometheusTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, std::string>> {};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndUrls, StatsHandlerPrometheusWithTextReadoutsTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values("/stats?format=prometheus&text_readouts",
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
