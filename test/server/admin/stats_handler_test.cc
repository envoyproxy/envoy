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
using testing::StartsWith;

namespace Envoy {
namespace Server {

class StatsHandlerTest {
public:
  StatsHandlerTest() : alloc_(symbol_table_), pool_(symbol_table_) {
    store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_);
    store_->addSink(sink_);
  }

  static std::string
  statsAsJsonHandler(std::map<std::string, uint64_t>& all_stats,
                     std::map<std::string, std::string>& all_text_readouts,
                     const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                     const bool used_only,
                     const absl::optional<std::string> histogram_buckets_value = absl::nullopt,
                     const absl::optional<std::regex> regex = absl::nullopt) {
    return StatsHandler::statsAsJson(all_stats, all_text_readouts, all_histograms, used_only,
                                     histogram_buckets_value, regex, true /*pretty_print*/);
  }

  Stats::StatName makeStat(absl::string_view name) { return pool_.add(name); }
  Stats::CustomStatNamespaces& customNamespaces() { return custom_namespaces_; }

  void shutdownThreading() {
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  Stats::SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  Stats::AllocatorImpl alloc_;
  Stats::MockSink sink_;
  Stats::ThreadLocalStoreImplPtr store_;
  Stats::StatNamePool pool_;
  Stats::CustomStatNamespacesImpl custom_namespaces_;
};

class AdminStatsTest : public StatsHandlerTest,
                       public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminStatsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminStatsTest, HandlerStatsInvalidFormat) {
  const std::string url = "/stats?format=blergh";
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);
  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::NotFound, code);
  EXPECT_EQ("usage: /stats?format=json  or /stats?format=prometheus \n\n", data.toString());
}

TEST_P(AdminStatsTest, HandlerStatsPlainText) {
  const std::string url = "/stats";
  Http::TestResponseHeaderMapImpl response_headers, used_response_headers;
  Buffer::OwnedImpl data, used_data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

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

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  constexpr char expected[] =
      "t: \"hello world\"\n"
      "c1: 10\n"
      "c2: 20\n"
      "h1: P0(200.0,200.0) P25(202.5,202.5) P50(205.0,205.0) P75(207.5,207.5) "
      "P90(209.0,209.0) P95(209.5,209.5) P99(209.9,209.9) P99.5(209.95,209.95) "
      "P99.9(209.99,209.99) P100(210.0,210.0)\n"
      "h2: P0(100.0,100.0) P25(102.5,102.5) P50(105.0,105.0) P75(107.5,107.5) "
      "P90(109.0,109.0) P95(109.5,109.5) P99(109.9,109.9) P99.5(109.95,109.95) "
      "P99.9(109.99,109.99) P100(110.0,110.0)\n";
  EXPECT_EQ(expected, data.toString());

  code = handler.handlerStats(url + "?usedonly", used_response_headers, used_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ(expected, used_data.toString());

  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsCumulative) {
  const std::string url = "/stats?histogram_buckets=cumulative";
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,0) "
            "B500(1,1) B1000(1,1) B2500(1,1) B5000(1,1) B10000(1,1) B30000(1,1) B60000(1,1) "
            "B300000(1,1) B600000(1,1) B1.8e+06(1,1) B3.6e+06(1,1)\n",
            data.toString());
  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsDisjoint) {
  const std::string url = "/stats?histogram_buckets=disjoint";
  Http::TestResponseHeaderMapImpl response_headers, used_response_headers,
      used_and_filter_response_headers;
  Buffer::OwnedImpl data, used_data, used_and_filter_data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

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

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,1) "
            "B500(1,2) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\nh2: No recorded values\n",
            data.toString());

  code = handler.handlerStats(url + "&usedonly", used_response_headers, used_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ("h1: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,1) "
            "B500(1,2) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\n",
            used_data.toString());

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 300));
  h2.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  code = handler.handlerStats(url + "&usedonly&filter=h2", used_and_filter_response_headers,
                              used_and_filter_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_EQ("h2: B0.5(0,0) B1(0,0) B5(0,0) B10(0,0) B25(0,0) B50(0,0) B100(0,0) B250(0,0) "
            "B500(1,1) B1000(0,0) B2500(0,0) B5000(0,0) B10000(0,0) B30000(0,0) B60000(0,0) "
            "B300000(0,0) B600000(0,0) B1.8e+06(0,0) B3.6e+06(0,0)\n",
            used_and_filter_data.toString());
  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsPlainTextHistogramBucketsInvalid) {
  const std::string url = "/stats?histogram_buckets=invalid_input";
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);
  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::BadRequest, code);
  EXPECT_EQ("usage: /stats?histogram_buckets=cumulative  or /stats?histogram_buckets=disjoint \n",
            data.toString());
}

TEST_P(AdminStatsTest, HandlerStatsJsonNoHistograms) {
  const std::string url = "/stats?format=json&usedonly";
  Http::TestResponseHeaderMapImpl response_headers, cumulative_response_headers,
      disjoint_response_headers;
  Buffer::OwnedImpl data, cumulative_data, disjoint_data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

  Stats::Counter& c1 = store_->counterFromString("c1");

  c1.add(10);

  store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  store_->mergeHistograms([]() -> void {});

  const std::string expected_json = R"EOF({
    "stats": [
        {
            "name":"c1",
            "value":10,
        }
    ]
})EOF";

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

  EXPECT_THAT(expected_json, JsonStringEq(data.toString()));

  code = handler.handlerStats(url + "&histogram_buckets=cumulative", cumulative_response_headers, cumulative_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

  EXPECT_THAT(expected_json, JsonStringEq(cumulative_data.toString()));

  code = handler.handlerStats(url + "&histogram_buckets=disjoint", disjoint_response_headers, disjoint_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

  EXPECT_THAT(expected_json, JsonStringEq(disjoint_data.toString()));

  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsJsonHistogramBucketsCumulative) {
  const std::string url = "/stats?histogram_buckets=cumulative&format=json";
  Http::TestResponseHeaderMapImpl response_headers, used_response_headers,
      used_and_filter_response_headers;
  Buffer::OwnedImpl data, used_data, used_and_filter_data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);
  
  store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c2.add(20);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

  const std::string expected_json = R"EOF({
    "stats": [
        {
            "name":"c1",
            "value":0
        },
        {
            "name":"c2",
            "value":20
        },
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(data.toString()));

  code = handler.handlerStats(url + "&usedonly", used_response_headers, used_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  const std::string expected_json_used = R"EOF({
    "stats": [
        {
            "name":"c2",
            "value":20
        },
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used, JsonStringEq(used_data.toString()));

  code = handler.handlerStats(url + "&usedonly&filter=h1", used_and_filter_response_headers,
                              used_and_filter_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  const std::string expected_json_used_and_filter = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        },
                        {
                            "interval": 1,
                            "cumulative": 3
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used_and_filter, JsonStringEq(used_and_filter_data.toString()));

  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsJsonHistogramBucketsDisjoint) {
  const std::string url = "/stats?histogram_buckets=disjoint&format=json";
  Http::TestResponseHeaderMapImpl response_headers, used_response_headers,
      used_and_filter_response_headers;
  Buffer::OwnedImpl data, used_data, used_and_filter_data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

  store_->counterFromString("c1");
  Stats::Counter& c2 = store_->counterFromString("c2");

  c2.add(20);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 300));
  h1.recordValue(300);

  store_->mergeHistograms([]() -> void {});

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

  const std::string expected_json = R"EOF({
    "stats": [
        {
            "name":"c1",
            "value":0
        },
        {
            "name":"c2",
            "value":20
        },
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json, JsonStringEq(data.toString()));

  code = handler.handlerStats(url + "&usedonly", used_response_headers, used_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  const std::string expected_json_used = R"EOF({
    "stats": [
        {
            "name":"c2",
            "value":20
        },
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used, JsonStringEq(used_data.toString()));

  code = handler.handlerStats(url + "&usedonly&filter=h1", used_and_filter_response_headers,
                              used_and_filter_data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  const std::string expected_json_used_and_filter = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_used_and_filter, JsonStringEq(used_and_filter_data.toString()));

  shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJsonHistogramBucketsMultipleHistograms) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 200));
  h1.recordValue(200);

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  h2.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  h1.recordValue(100);

  store_->mergeHistograms([]() -> void {});

  std::vector<Stats::ParentHistogramSharedPtr> histograms = store_->histograms();
  std::sort(histograms.begin(), histograms.end(),
            [](const Stats::ParentHistogramSharedPtr& a,
               const Stats::ParentHistogramSharedPtr& b) -> bool { return a->name() < b->name(); });
  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> all_text_readouts;
  std::string actual_json_cumulative = statsAsJsonHandler(all_stats, all_text_readouts, histograms, false, absl::optional<std::string>("cumulative"));

  const std::string expected_json_cumulative = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        }
                    ]
                },
                {
                    "name": "h2",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_cumulative, JsonStringEq(actual_json_cumulative));

  std::string actual_json_disjoint = statsAsJsonHandler(all_stats, all_text_readouts, histograms, false, absl::optional<std::string>("disjoint"));

  const std::string expected_json_disjoint = R"EOF({
    "stats": [
        {
            "histograms": [
                {
                    "name": "h1",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 1,
                            "cumulative": 2
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        }
                    ]
                },
                {
                    "name": "h2",
                    "supported_buckets": [
                        0.5,
                        1,
                        5,
                        10,
                        25,
                        50,
                        100,
                        250,
                        500,
                        1000,
                        2500,
                        5000,
                        10000,
                        30000,
                        60000,
                        300000,
                        600000,
                        1800000,
                        3600000
                    ],
                    "computed_buckets": [
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 1
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        },
                        {
                            "interval": 0,
                            "cumulative": 0
                        }
                    ]
                }
            ]
        }
    ]
})EOF";

  EXPECT_THAT(expected_json_disjoint, JsonStringEq(actual_json_disjoint));
  
  shutdownThreading();
}

TEST_P(AdminStatsTest, HandlerStatsJson) {
  const std::string url = "/stats?format=json";
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
  MockInstance instance;
  store_->initializeThreading(main_thread_dispatcher_, tls_);
  EXPECT_CALL(instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
  EXPECT_CALL(instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
  StatsHandler handler(instance);

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

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);

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

  EXPECT_THAT(expected_json_old, JsonStringEq(data.toString()));

  shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

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

  std::vector<Stats::ParentHistogramSharedPtr> histograms = store_->histograms();
  std::sort(histograms.begin(), histograms.end(),
            [](const Stats::ParentHistogramSharedPtr& a,
               const Stats::ParentHistogramSharedPtr& b) -> bool { return a->name() < b->name(); });
  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> all_text_readouts;
  std::string actual_json = statsAsJsonHandler(all_stats, all_text_readouts, histograms, false);

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
  shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

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

  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> all_text_readouts;
  std::string actual_json =
      statsAsJsonHandler(all_stats, all_text_readouts, store_->histograms(), true);

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
  shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

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

  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> all_text_readouts;
  std::string actual_json =
      statsAsJsonHandler(all_stats, all_text_readouts, store_->histograms(), false, absl::nullopt,
                         absl::optional<std::regex>{std::regex("[a-z]1")});

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
  shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

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

  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> all_text_readouts;
  std::string actual_json =
      statsAsJsonHandler(all_stats, all_text_readouts, store_->histograms(), true, absl::nullopt,
                         absl::optional<std::regex>{std::regex("h[12]")});

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
  shutdownThreading();
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

  // We can't test RecentLookups in admin unit tests as it doesn't work with a
  // fake symbol table. However we cover this solidly in integration tests.
}

class StatsHandlerPrometheusTest : public StatsHandlerTest {
public:
  Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl data;
  MockAdminStream admin_stream;
  Configuration::MockStatsConfig stats_config;
  Api::MockApi api;

  std::shared_ptr<MockInstance> setupMockedInstance() {
    auto instance = std::make_shared<MockInstance>();
    EXPECT_CALL(stats_config, flushOnAdmin()).WillRepeatedly(testing::Return(false));
    store_->initializeThreading(main_thread_dispatcher_, tls_);
    EXPECT_CALL(*instance, stats()).WillRepeatedly(testing::ReturnRef(*store_));
    EXPECT_CALL(*instance, statsConfig()).WillRepeatedly(testing::ReturnRef(stats_config));
    EXPECT_CALL(api, customStatNamespaces()).WillRepeatedly(testing::ReturnRef(customNamespaces()));
    EXPECT_CALL(*instance, api()).WillRepeatedly(testing::ReturnRef(api));
    return instance;
  }

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
  std::string url = "/stats?format=prometheus";

  createTestStats();
  std::shared_ptr<MockInstance> instance = setupMockedInstance();
  StatsHandler handler(*instance);

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20

# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12

)EOF";

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_THAT(expected_response, data.toString());

  shutdownThreading();
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
  std::string url = std::get<1>(GetParam());

  createTestStats();
  std::shared_ptr<MockInstance> instance = setupMockedInstance();
  StatsHandler handler(*instance);

  const std::string expected_response = R"EOF(# TYPE envoy_cluster_upstream_cx_total counter
envoy_cluster_upstream_cx_total{cluster="c1"} 10
envoy_cluster_upstream_cx_total{cluster="c2"} 20

# TYPE envoy_cluster_upstream_cx_active gauge
envoy_cluster_upstream_cx_active{cluster="c1"} 11
envoy_cluster_upstream_cx_active{cluster="c2"} 12

# TYPE envoy_control_plane_identifier gauge
envoy_control_plane_identifier{cluster="c1",text_value="cp-1"} 0

)EOF";

  Http::Code code = handler.handlerStats(url, response_headers, data, admin_stream);
  EXPECT_EQ(Http::Code::OK, code);
  EXPECT_THAT(expected_response, data.toString());

  shutdownThreading();
}

} // namespace Server
} // namespace Envoy
