#include <fstream>
#include <unordered_map>

#include "envoy/json/json_object.h"
#include "envoy/runtime/runtime.h"

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/profiler/profiler.h"
#include "common/stats/stats_impl.h"
#include "common/stats/thread_local_store.h"

#include "server/http/admin.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::_;

namespace Envoy {
namespace Server {

class AdminStatsTest : public testing::TestWithParam<Network::Address::IpVersion>,
                       public Stats::RawStatDataAllocator {
public:
public:
  AdminStatsTest() {
    ON_CALL(*this, alloc(_))
        .WillByDefault(Invoke(
            [this](const std::string& name) -> Stats::RawStatData* { return alloc_.alloc(name); }));

    ON_CALL(*this, free(_)).WillByDefault(Invoke([this](Stats::RawStatData& data) -> void {
      return alloc_.free(data);
    }));

    EXPECT_CALL(*this, alloc("stats.overflow"));
    store_.reset(new Stats::ThreadLocalStoreImpl(*this));
    store_->addSink(sink_);
  }

  static std::string
  statsAsJsonHandler(std::map<std::string, uint64_t>& all_stats,
                     const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                     const bool used_only) {
    return AdminImpl::statsAsJson(all_stats, all_histograms, used_only, true);
  }

  MOCK_METHOD1(alloc, Stats::RawStatData*(const std::string& name));
  MOCK_METHOD1(free, void(Stats::RawStatData& data));

  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::TestAllocator alloc_;
  Stats::MockSink sink_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> store_;
};

class AdminFilterTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  // TODO(mattklein123): Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", TestEnvironment::temporaryPath("envoy.prof"),
               TestEnvironment::temporaryPath("admin.address"),
               Network::Test::getCanonicalLoopbackAddress(GetParam()), server_,
               listener_scope_.createScope("listener.admin.")),
        filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdminStatsTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(AdminStatsTest, StatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram("h1");
  Stats::Histogram& h2 = store_->histogram("h2");

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

  EXPECT_CALL(*this, free(_));

  std::map<std::string, uint64_t> all_stats;

  std::string actual_json = statsAsJsonHandler(all_stats, store_->histograms(), false);

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
                    99.9,
                    100.0
                ],
                "computed_quantiles": [
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
                                "cumulative": 109.99
                            },
                            {
                                "interval": null,
                                "cumulative": 110.0
                            }
                        ]
                    },
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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram("h1");
  Stats::Histogram& h2 = store_->histogram("h2");

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

  EXPECT_CALL(*this, free(_));

  std::map<std::string, uint64_t> all_stats;

  std::string actual_json = statsAsJsonHandler(all_stats, store_->histograms(), false);

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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

INSTANTIATE_TEST_CASE_P(IpVersions, AdminFilterTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeHeaders(request_headers_, true);
}

TEST_P(AdminFilterTest, Body) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeData(data, true);
}

TEST_P(AdminFilterTest, Trailers) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  filter_.decodeData(data, false);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeTrailers(request_headers_);
}

class AdminInstanceTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminInstanceTest()
      : address_out_path_(TestEnvironment::temporaryPath("admin.address")),
        cpu_profile_path_(TestEnvironment::temporaryPath("envoy.prof")),
        admin_("/dev/null", cpu_profile_path_, address_out_path_,
               Network::Test::getCanonicalLoopbackAddress(GetParam()), server_,
               listener_scope_.createScope("listener.admin.")),
        request_headers_{{":path", "/"}}, admin_filter_(admin_) {

    EXPECT_EQ(std::chrono::milliseconds(100), admin_.drainTimeout());
    admin_.tracingStats().random_sampling_.inc();
    EXPECT_TRUE(admin_.setCurrentClientCertDetails().empty());
  }

  Http::Code runCallback(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                         Buffer::Instance& response, absl::string_view method) {
    request_headers_.insertMethod().value(method.data(), method.size());
    admin_filter_.decodeHeaders(request_headers_, false);
    return admin_.runCallback(path_and_query, response_headers, response, admin_filter_);
  }

  Http::Code getCallback(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                         Buffer::Instance& response) {
    return runCallback(path_and_query, response_headers, response,
                       Http::Headers::get().MethodValues.Get);
  }

  Http::Code postCallback(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                          Buffer::Instance& response) {
    return runCallback(path_and_query, response_headers, response,
                       Http::Headers::get().MethodValues.Post);
  }

  std::string address_out_path_;
  std::string cpu_profile_path_;
  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
  Http::TestHeaderMapImpl request_headers_;
  Server::AdminFilter admin_filter_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdminInstanceTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);
// Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
// a real profiler linked in (successful call to startProfiler). startProfiler
// requies tcmalloc.
#ifdef TCMALLOC

TEST_P(AdminInstanceTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

#endif

TEST_P(AdminInstanceTest, MutatesWarnWithGet) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  const std::string path("/healthcheck/fail");
  // TODO(jmarantz): the call to getCallback should be made to fail, but as an interim we will
  // just issue a warning, so that scripts using curl GET comamnds to mutate state can be fixed.
  EXPECT_LOG_CONTAINS("warning",
                      "admin path \"" + path + "\" mutates state, method=GET rather than POST",
                      EXPECT_EQ(Http::Code::OK, getCallback(path, header_map, data)));
}

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path("/dev/null",
                                   TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   "", Network::Test::getCanonicalLoopbackAddress(GetParam()),
                                   server_, listener_scope_.createScope("listener.admin."));
  Http::HeaderMapImpl header_map;
  const absl::string_view post = Http::Headers::get().MethodValues.Post;
  request_headers_.insertMethod().value(post.data(), post.size());
  admin_filter_.decodeHeaders(request_headers_, false);
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::InternalServerError,
                           admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map,
                                                              data, admin_filter_)));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  std::unique_ptr<AdminImpl> admin_bad_address_out_path;
  EXPECT_LOG_CONTAINS(
      "critical", "cannot open admin address output file " + bad_path + " for writing.",
      admin_bad_address_out_path =
          std::make_unique<AdminImpl>("/dev/null", cpu_profile_path_, bad_path,
                                      Network::Test::getCanonicalLoopbackAddress(GetParam()),
                                      server_, listener_scope_.createScope("listener.admin.")));
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // Test removable handler.
  EXPECT_NO_LOGS(EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true, false)));
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::NotFound, getCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, false, false));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addHandler("/foo/bar", "hello", callback, false, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/foo<script>alert('hi')</script>\" contains invalid character '<'",
                      EXPECT_FALSE(admin_.addHandler("/foo<script>alert('hi')</script>", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/bar?queryShouldNotBeInPrefix\" contains invalid character '?'",
                      EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string planets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", planets, callback, true, false));

  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_THAT(std::string(content_type.getStringView()), testing::HasSubstr("text/html"));
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0));
}

TEST_P(AdminInstanceTest, HelpUsesFormForMutations) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const std::string logging_action = "<form action='/logging' method='post'";
  const std::string stats_href = "<a href='/stats'";
  EXPECT_NE(-1, response.search(logging_action.data(), logging_action.size(), 0));
  EXPECT_NE(-1, response.search(stats_href.data(), stats_href.size(), 0));
}

TEST_P(AdminInstanceTest, ConfigDump) {
  Buffer::OwnedImpl response;
  Http::HeaderMapImpl header_map;
  auto entry = admin_.getConfigTracker().add("foo", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bar");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": {
  "foo": {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bar"
  }
 }
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
  std::string output = TestUtility::bufferToString(response);
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, Runtime) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;
  auto layer1 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  auto layer2 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  std::unordered_map<std::string, Runtime::Snapshot::Entry> entries1{
      {"string_key", {"foo", {}}}, {"int_key", {"1", {1}}}, {"other_key", {"bar", {}}}};
  std::unordered_map<std::string, Runtime::Snapshot::Entry> entries2{
      {"string_key", {"override", {}}}, {"extra_key", {"bar", {}}}};

  ON_CALL(*layer1, name()).WillByDefault(testing::ReturnRefOfCopy(std::string{"layer1"}));
  ON_CALL(*layer1, values()).WillByDefault(testing::ReturnRef(entries1));
  ON_CALL(*layer2, name()).WillByDefault(testing::ReturnRefOfCopy(std::string{"layer2"}));
  ON_CALL(*layer2, values()).WillByDefault(testing::ReturnRef(entries2));

  std::vector<Runtime::Snapshot::OverrideLayerConstPtr> layers;
  layers.push_back(std::move(layer1));
  layers.push_back(std::move(layer2));
  EXPECT_CALL(snapshot, getLayers()).WillRepeatedly(testing::ReturnRef(layers));

  const std::string expected_json = R"EOF({
    "layers": [
        "layer1",
        "layer2"
    ],
    "entries": {
        "extra_key": {
            "layer_values": [
                "",
                "bar"
            ],
            "final_value": "bar"
        },
        "int_key": {
            "layer_values": [
                "1",
                ""
            ],
            "final_value": "1"
        },
        "other_key": {
            "layer_values": [
                "bar",
                ""
            ],
            "final_value": "bar"
        },
        "string_key": {
            "layer_values": [
                "foo",
                "override"
            ],
            "final_value": "override"
        }
    }
})EOF";

  EXPECT_CALL(loader, snapshot()).WillRepeatedly(testing::ReturnPointee(&snapshot));
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));
  EXPECT_EQ(Http::Code::OK, getCallback("/runtime", header_map, response));
  EXPECT_EQ(expected_json, TestUtility::bufferToString(response));
}

TEST_P(AdminInstanceTest, RuntimeModify) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Runtime::MockLoader loader;
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  std::unordered_map<std::string, std::string> overrides;
  overrides["foo"] = "bar";
  overrides["x"] = "42";
  overrides["nothing"] = "";
  EXPECT_CALL(loader, mergeValues(overrides)).Times(1);
  EXPECT_EQ(Http::Code::OK,
            getCallback("/runtime_modify?foo=bar&x=42&nothing=", header_map, response));
  EXPECT_EQ("OK\n", TestUtility::bufferToString(response));
}

TEST_P(AdminInstanceTest, RuntimeModifyNoArguments) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  EXPECT_EQ(Http::Code::BadRequest, getCallback("/runtime_modify", header_map, response));
  EXPECT_TRUE(absl::StartsWith(TestUtility::bufferToString(response), "usage:"));
}

TEST_P(AdminInstanceTest, TracingStatsDisabled) {
  const std::string& name = admin_.tracingStats().service_forced_.name();
  for (Stats::CounterSharedPtr counter : server_.stats().counters()) {
    EXPECT_NE(counter->name(), name) << "Unexpected tracing stat found in server stats: " << name;
  }
}

TEST(PrometheusStatsFormatter, MetricName) {
  std::string raw = "vulture.eats-liver";
  std::string expected = "envoy_vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST(PrometheusStatsFormatter, FormattedTags) {
  // If value has - then it should be replaced by _ .
  std::vector<Stats::Tag> tags;
  Stats::Tag tag1 = {"a.tag-name", "a.tag-value"};
  Stats::Tag tag2 = {"another_tag_name", "another.tag-value"};
  tags.push_back(tag1);
  tags.push_back(tag2);
  std::string expected = "a_tag_name=\"a_tag_value\",another_tag_name=\"another_tag_value\"";
  auto actual = PrometheusStatsFormatter::formattedTags(tags);
  EXPECT_EQ(expected, actual);
}

TEST(PrometheusStatsFormatter, MetricNameCollison) {

  // Create two counters and two gauges with each pair having the same name,
  // but having different tag names and values.
  //`statsAsPrometheus()` should return two implying it found two unique stat names

  Stats::HeapRawStatDataAllocator alloc;
  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;

  {
    std::string name = "cluster.test_cluster_1.upstream_cx_total";
    std::vector<Stats::Tag> cluster_tags;
    Stats::Tag tag = {"a.tag-name", "a.tag-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::CounterSharedPtr c = std::make_shared<Stats::CounterImpl>(*data, alloc, std::move(name),
                                                                     std::move(cluster_tags));
    counters.push_back(c);
  }

  {
    std::string name = "cluster.test_cluster_1.upstream_cx_total";
    std::vector<Stats::Tag> cluster_tags;
    Stats::Tag tag = {"another_tag_name", "another_tag-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::CounterSharedPtr c = std::make_shared<Stats::CounterImpl>(*data, alloc, std::move(name),
                                                                     std::move(cluster_tags));
    counters.push_back(c);
  }

  {
    std::vector<Stats::Tag> cluster_tags;
    std::string name = "cluster.test_cluster_2.upstream_cx_total";
    Stats::Tag tag = {"another_tag_name_3", "another_tag_3-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::GaugeSharedPtr g =
        std::make_shared<Stats::GaugeImpl>(*data, alloc, std::move(name), std::move(cluster_tags));
    gauges.push_back(g);
  }

  {
    std::vector<Stats::Tag> cluster_tags;
    std::string name = "cluster.test_cluster_2.upstream_cx_total";
    Stats::Tag tag = {"another_tag_name_4", "another_tag_4-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::GaugeSharedPtr g =
        std::make_shared<Stats::GaugeImpl>(*data, alloc, std::move(name), std::move(cluster_tags));
    gauges.push_back(g);
  }

  Buffer::OwnedImpl response;
  EXPECT_EQ(2UL, PrometheusStatsFormatter::statsAsPrometheus(counters, gauges, response));
}

TEST(PrometheusStatsFormatter, UniqueMetricName) {

  // Create two counters and two gauges, all with unique names.
  // statsAsPrometheus() should return four implying it found
  // four unique stat names.

  Stats::HeapRawStatDataAllocator alloc;
  std::vector<Stats::CounterSharedPtr> counters;
  std::vector<Stats::GaugeSharedPtr> gauges;

  {
    std::string name = "cluster.test_cluster_1.upstream_cx_total";
    std::vector<Stats::Tag> cluster_tags;
    Stats::Tag tag = {"a.tag-name", "a.tag-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::CounterSharedPtr c = std::make_shared<Stats::CounterImpl>(*data, alloc, std::move(name),
                                                                     std::move(cluster_tags));
    counters.push_back(c);
  }

  {
    std::string name = "cluster.test_cluster_2.upstream_cx_total";
    std::vector<Stats::Tag> cluster_tags;
    Stats::Tag tag = {"another_tag_name", "another_tag-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::CounterSharedPtr c = std::make_shared<Stats::CounterImpl>(*data, alloc, std::move(name),
                                                                     std::move(cluster_tags));
    counters.push_back(c);
  }

  {
    std::vector<Stats::Tag> cluster_tags;
    std::string name = "cluster.test_cluster_3.upstream_cx_total";
    Stats::Tag tag = {"another_tag_name_3", "another_tag_3-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::GaugeSharedPtr g =
        std::make_shared<Stats::GaugeImpl>(*data, alloc, std::move(name), std::move(cluster_tags));
    gauges.push_back(g);
  }

  {
    std::vector<Stats::Tag> cluster_tags;
    std::string name = "cluster.test_cluster_4.upstream_cx_total";
    Stats::Tag tag = {"another_tag_name_4", "another_tag_4-value"};
    cluster_tags.push_back(tag);
    Stats::RawStatData* data = alloc.alloc(name);
    Stats::GaugeSharedPtr g =
        std::make_shared<Stats::GaugeImpl>(*data, alloc, std::move(name), std::move(cluster_tags));
    gauges.push_back(g);
  }

  Buffer::OwnedImpl response;
  EXPECT_EQ(4UL, PrometheusStatsFormatter::statsAsPrometheus(counters, gauges, response));
}

} // namespace Server
} // namespace Envoy
