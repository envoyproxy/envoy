#include <algorithm>
#include <fstream>
#include <regex>
#include <unordered_map>

#include "envoy/admin/v2alpha/memory.pb.h"
#include "envoy/admin/v2alpha/server_info.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/profiler/profiler.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/thread_local_store.h"

#include "server/http/admin.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "test/mocks/http/mocks.h"
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

using testing::_;
using testing::AllOf;
using testing::EndsWith;
using testing::Ge;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::StartsWith;

namespace Envoy {
namespace Server {

class AdminStatsTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminStatsTest()
      : symbol_table_(Stats::SymbolTableCreator::makeSymbolTable()), alloc_(*symbol_table_) {
    store_ = std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_);
    store_->addSink(sink_);
  }

  static std::string
  statsAsJsonHandler(std::map<std::string, uint64_t>& all_stats,
                     const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                     const bool used_only, const absl::optional<std::regex> regex = absl::nullopt) {
    return AdminImpl::statsAsJson(all_stats, all_histograms, used_only, regex,
                                  true /*pretty_print*/);
  }

  Stats::SymbolTablePtr symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::AllocatorImpl alloc_;
  Stats::MockSink sink_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> store_;
};

class AdminFilterTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminFilterTest()
      : admin_(TestEnvironment::temporaryPath("envoy.prof"), server_),
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

// Check default implementations the admin class picks up.
TEST_P(AdminFilterTest, MiscFunctions) {
  EXPECT_EQ(false, admin_.preserveExternalRequestId());
  Http::MockFilterChainFactoryCallbacks mock_filter_chain_factory_callbacks;
  EXPECT_EQ(false,
            admin_.createUpgradeFilterChain("", nullptr, mock_filter_chain_factory_callbacks));
  EXPECT_TRUE(nullptr != admin_.scopedRouteConfigProvider());
  EXPECT_EQ(Http::ConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE,
            admin_.serverHeaderTransformation());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminStatsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminStatsTest, StatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogram("h2", Stats::Histogram::Unit::Unspecified);

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
  std::string actual_json = statsAsJsonHandler(all_stats, histograms, false);

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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJson) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogram("h2", Stats::Histogram::Unit::Unspecified);

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
  std::string actual_json = statsAsJsonHandler(all_stats, store_->histograms(), true);

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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

TEST_P(AdminStatsTest, StatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram("h1", Stats::Histogram::Unit::Unspecified);
  Stats::Histogram& h2 = store_->histogram("h2", Stats::Histogram::Unit::Unspecified);

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
  std::string actual_json = statsAsJsonHandler(all_stats, store_->histograms(), false,
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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

TEST_P(AdminStatsTest, UsedOnlyStatsAsJsonFilterString) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  Stats::Histogram& h1 = store_->histogram(
      "h1_matches", Stats::Histogram::Unit::Unspecified); // Will match, be used, and print
  Stats::Histogram& h2 = store_->histogram(
      "h2_matches", Stats::Histogram::Unit::Unspecified); // Will match but not be used
  Stats::Histogram& h3 = store_->histogram(
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
  std::string actual_json = statsAsJsonHandler(all_stats, store_->histograms(), true,
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

  EXPECT_EQ(expected_json, actual_json);
  store_->shutdownThreading();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminFilterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, true));
}

TEST_P(AdminFilterTest, Body) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, true));
}

TEST_P(AdminFilterTest, Trailers) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, false));
  EXPECT_CALL(callbacks_, decodingBuffer());
  filter_.getRequestBody();
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_.decodeTrailers(request_headers_));
}

class AdminInstanceTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminInstanceTest()
      : address_out_path_(TestEnvironment::temporaryPath("admin.address")),
        cpu_profile_path_(TestEnvironment::temporaryPath("envoy.prof")),
        admin_(cpu_profile_path_, server_), request_headers_{{":path", "/"}},
        admin_filter_(admin_) {
    admin_.startHttpListener("/dev/null", address_out_path_,
                             Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                             listener_scope_.createScope("listener.admin."));
    EXPECT_EQ(std::chrono::milliseconds(100), admin_.drainTimeout());
    admin_.tracingStats().random_sampling_.inc();
    EXPECT_TRUE(admin_.setCurrentClientCertDetails().empty());
    admin_filter_.setDecoderFilterCallbacks(callbacks_);
  }

  Http::Code runCallback(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                         Buffer::Instance& response, absl::string_view method,
                         absl::string_view body = absl::string_view()) {
    if (!body.empty()) {
      request_headers_.insertContentType().value(
          Http::Headers::get().ContentTypeValues.FormUrlEncoded);
      callbacks_.buffer_ = std::make_unique<Buffer::OwnedImpl>(body);
    }

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
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, AdminCpuProfiler) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;

  // Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
  // a real profiler linked in (successful call to startProfiler).
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
#else
  EXPECT_EQ(Http::Code::InternalServerError,
            postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
#endif

  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, AdminHeapProfilerOnRepeatedRequest) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  auto repeatResultCode = Http::Code::BadRequest;
#ifndef PROFILER_AVAILABLE
  repeatResultCode = Http::Code::NotImplemented;
#endif

  postCallback("/heapprofiler?enable=y", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=y", header_map, data));

  postCallback("/heapprofiler?enable=n", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=n", header_map, data));
}

TEST_P(AdminInstanceTest, AdminHeapProfiler) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;

  // The below flow need to begin with the profiler not running
  Profiler::Heap::stopProfiler();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=n", header_map, data));
#else
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=n", header_map, data));
#endif

  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
}

TEST_P(AdminInstanceTest, MutatesErrorWithGet) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  const std::string path("/healthcheck/fail");
  // TODO(jmarantz): the call to getCallback should be made to fail, but as an interim we will
  // just issue a warning, so that scripts using curl GET commands to mutate state can be fixed.
  EXPECT_LOG_CONTAINS("error",
                      "admin path \"" + path + "\" mutates state, method=GET rather than POST",
                      EXPECT_EQ(Http::Code::MethodNotAllowed, getCallback(path, header_map, data)));
}

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path(TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   server_);
  Http::HeaderMapImpl header_map;
  const absl::string_view post = Http::Headers::get().MethodValues.Post;
  request_headers_.insertMethod().value(post.data(), post.size());
  admin_filter_.decodeHeaders(request_headers_, false);
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::InternalServerError,
                           admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map,
                                                              data, admin_filter_)));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, StatsInvalidRegex) {
  Http::HeaderMapImpl header_map;
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
  Http::HeaderMapImpl header_map;
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

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(cpu_profile_path_, server_);
  EXPECT_LOG_CONTAINS(
      "critical", "cannot open admin address output file " + bad_path + " for writing.",
      admin_bad_address_out_path.startHttpListener(
          "/dev/null", bad_path, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
          listener_scope_.createScope("listener.admin.")));
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
  const std::string logging_action = "<form action='logging' method='post'";
  const std::string stats_href = "<a href='stats'";
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
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bar"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, ConfigDumpMaintainsOrder) {
  // Add configs in random order and validate config_dump dumps in the order.
  auto bootstrap_entry = admin_.getConfigTracker().add("bootstrap", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bootstrap_config");
    return msg;
  });
  auto route_entry = admin_.getConfigTracker().add("routes", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("routes_config");
    return msg;
  });
  auto listener_entry = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("listeners_config");
    return msg;
  });
  auto cluster_entry = admin_.getConfigTracker().add("clusters", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("clusters_config");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bootstrap_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "clusters_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "listeners_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "routes_config"
  }
 ]
}
)EOF";
  // Run it multiple times and validate that order is preserved.
  for (size_t i = 0; i < 5; i++) {
    Buffer::OwnedImpl response;
    Http::HeaderMapImpl header_map;
    EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
    const std::string output = response.toString();
    EXPECT_EQ(expected_json, output);
  }
}

TEST_P(AdminInstanceTest, Memory) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/memory", header_map, response));
  const std::string output_json = response.toString();
  envoy::admin::v2alpha::Memory output_proto;
  TestUtility::loadFromJson(output_json, output_proto);
  EXPECT_THAT(output_proto,
              AllOf(Property(&envoy::admin::v2alpha::Memory::allocated, Ge(0)),
                    Property(&envoy::admin::v2alpha::Memory::heap_size, Ge(0)),
                    Property(&envoy::admin::v2alpha::Memory::pageheap_unmapped, Ge(0)),
                    Property(&envoy::admin::v2alpha::Memory::pageheap_free, Ge(0)),
                    Property(&envoy::admin::v2alpha::Memory::total_thread_cache, Ge(0))));
}

TEST_P(AdminInstanceTest, ContextThatReturnsNullCertDetails) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  // Setup a context that returns null cert details.
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  envoy::api::v2::auth::UpstreamTlsContext config;
  Extensions::TransportSockets::Tls::ClientContextConfigImpl cfg(config, factory_context);
  Stats::IsolatedStoreImpl store;
  Envoy::Ssl::ClientContextSharedPtr client_ctx(
      server_.sslContextManager().createSslClientContext(store, cfg));

  const std::string expected_empty_json = R"EOF({
 "certificates": [
  {
   "ca_cert": [],
   "cert_chain": []
  }
 ]
}
)EOF";

  // Validate that cert details are null and /certs handles it correctly.
  EXPECT_EQ(nullptr, client_ctx->getCaCertInformation());
  EXPECT_TRUE(client_ctx->getCertChainInformation().empty());
  EXPECT_EQ(Http::Code::OK, getCallback("/certs", header_map, response));
  EXPECT_EQ(expected_empty_json, response.toString());
}

TEST_P(AdminInstanceTest, Runtime) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;
  auto layer1 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  auto layer2 = std::make_unique<NiceMock<Runtime::MockOverrideLayer>>();
  Runtime::Snapshot::EntryMap entries2{{"string_key", {"override", {}, {}, {}, {}}},
                                       {"extra_key", {"bar", {}, {}, {}, {}}}};
  Runtime::Snapshot::EntryMap entries1{{"string_key", {"foo", {}, {}, {}, {}}},
                                       {"int_key", {"1", 1, {}, {}, {}}},
                                       {"other_key", {"bar", {}, {}, {}, {}}}};

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
  EXPECT_EQ(expected_json, response.toString());
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
            postCallback("/runtime_modify?foo=bar&x=42&nothing=", header_map, response));
  EXPECT_EQ("OK\n", response.toString());
}

TEST_P(AdminInstanceTest, RuntimeModifyParamsInBody) {
  Runtime::MockLoader loader;
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  const std::string key = "routing.traffic_shift.foo";
  const std::string value = "numerator: 1\ndenominator: TEN_THOUSAND\n";
  const std::unordered_map<std::string, std::string> overrides = {{key, value}};
  EXPECT_CALL(loader, mergeValues(overrides)).Times(1);

  const std::string body = fmt::format("{}={}", key, value);
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, runCallback("/runtime_modify", header_map, response, "POST", body));
  EXPECT_EQ("OK\n", response.toString());
}

TEST_P(AdminInstanceTest, RuntimeModifyNoArguments) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  EXPECT_EQ(Http::Code::BadRequest, postCallback("/runtime_modify", header_map, response));
  EXPECT_TRUE(absl::StartsWith(response.toString(), "usage:"));
}

TEST_P(AdminInstanceTest, TracingStatsDisabled) {
  const std::string& name = admin_.tracingStats().service_forced_.name();
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    EXPECT_NE(counter->name(), name) << "Unexpected tracing stat found in server stats: " << name;
  }
}

TEST_P(AdminInstanceTest, ClustersJson) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_map.emplace(cluster.info_->name_, cluster);

  NiceMock<Upstream::Outlier::MockDetector> outlier_detector;
  ON_CALL(Const(cluster), outlierDetector()).WillByDefault(Return(&outlier_detector));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(6.0));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(9.0));

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(true));

  Upstream::MockHostSet* host_set = cluster.priority_set_.getMockHostSet(0);
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  envoy::api::v2::core::Locality locality;
  locality.set_region("test_region");
  locality.set_zone("test_zone");
  locality.set_sub_zone("test_sub_zone");
  ON_CALL(*host, locality()).WillByDefault(ReturnRef(locality));

  host_set->hosts_.emplace_back(host);
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://1.2.3.4:80");
  ON_CALL(*host, address()).WillByDefault(Return(address));
  const std::string hostname = "foo.com";
  ON_CALL(*host, hostname()).WillByDefault(ReturnRef(hostname));

  // Add stats in random order and validate that they come in order.
  Stats::PrimitiveCounter test_counter;
  test_counter.add(10);
  Stats::PrimitiveCounter rest_counter;
  rest_counter.add(10);
  Stats::PrimitiveCounter arest_counter;
  arest_counter.add(5);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters = {
      {"arest_counter", arest_counter},
      {"rest_counter", rest_counter},
      {"test_counter", test_counter},
  };
  Stats::PrimitiveGauge test_gauge;
  test_gauge.set(11);
  Stats::PrimitiveGauge atest_gauge;
  atest_gauge.set(10);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges = {
      {"atest_gauge", atest_gauge},
      {"test_gauge", test_gauge},
  };
  ON_CALL(*host, counters()).WillByDefault(Invoke([&counters]() { return counters; }));
  ON_CALL(*host, gauges()).WillByDefault(Invoke([&gauges]() { return gauges; }));

  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH))
      .WillByDefault(Return(false));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL))
      .WillByDefault(Return(true));

  ON_CALL(
      host->outlier_detector_,
      successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(43.2));
  ON_CALL(*host, weight()).WillByDefault(Return(5));
  ON_CALL(host->outlier_detector_,
          successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(93.2));
  ON_CALL(*host, priority()).WillByDefault(Return(6));

  Buffer::OwnedImpl response;
  Http::HeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters?format=json", header_map, response));
  std::string output_json = response.toString();
  envoy::admin::v2alpha::Clusters output_proto;
  TestUtility::loadFromJson(output_json, output_proto);

  const std::string expected_json = R"EOF({
 "cluster_statuses": [
  {
   "name": "fake_cluster",
   "success_rate_ejection_threshold": {
    "value": 6
   },
   "local_origin_success_rate_ejection_threshold": {
    "value": 9
   },
   "added_via_api": true,
   "host_statuses": [
    {
     "address": {
      "socket_address": {
       "protocol": "TCP",
       "address": "1.2.3.4",
       "port_value": 80
      }
     },
     "stats": [
       {
       "name": "arest_counter",
       "value": "5",
       "type": "COUNTER"
       },
       {
       "name": "rest_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "test_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "atest_gauge",
       "value": "10",
       "type": "GAUGE"
      },
      {
       "name": "test_gauge",
       "value": "11",
       "type": "GAUGE"
      },
     ],
     "health_status": {
      "eds_health_status": "DEGRADED",
      "failed_active_health_check": true,
      "failed_outlier_check": true,
      "failed_active_degraded_check": true,
      "pending_dynamic_removal": true
     },
     "success_rate": {
      "value": 43.2
     },
     "weight": 5,
     "hostname": "foo.com",
     "priority": 6,
     "local_origin_success_rate": {
      "value": 93.2
     }
    }
   ]
  }
 ]
}
)EOF";

  envoy::admin::v2alpha::Clusters expected_proto;
  TestUtility::loadFromJson(expected_json, expected_proto);

  // Ensure the protos created from each JSON are equivalent.
  EXPECT_THAT(output_proto, ProtoEq(expected_proto));

  // Ensure that the normal text format is used by default.
  Buffer::OwnedImpl response2;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters", header_map, response2));
  const std::string expected_text = R"EOF(fake_cluster::outlier::success_rate_average::0
fake_cluster::outlier::success_rate_ejection_threshold::6
fake_cluster::outlier::local_origin_success_rate_average::0
fake_cluster::outlier::local_origin_success_rate_ejection_threshold::9
fake_cluster::default_priority::max_connections::1
fake_cluster::default_priority::max_pending_requests::1024
fake_cluster::default_priority::max_requests::1024
fake_cluster::default_priority::max_retries::1
fake_cluster::high_priority::max_connections::1
fake_cluster::high_priority::max_pending_requests::1024
fake_cluster::high_priority::max_requests::1024
fake_cluster::high_priority::max_retries::1
fake_cluster::added_via_api::true
fake_cluster::1.2.3.4:80::arest_counter::5
fake_cluster::1.2.3.4:80::atest_gauge::10
fake_cluster::1.2.3.4:80::rest_counter::10
fake_cluster::1.2.3.4:80::test_counter::10
fake_cluster::1.2.3.4:80::test_gauge::11
fake_cluster::1.2.3.4:80::hostname::foo.com
fake_cluster::1.2.3.4:80::health_flags::/failed_active_hc/failed_outlier_check/degraded_active_hc/degraded_eds_health/pending_dynamic_removal
fake_cluster::1.2.3.4:80::weight::5
fake_cluster::1.2.3.4:80::region::test_region
fake_cluster::1.2.3.4:80::zone::test_zone
fake_cluster::1.2.3.4:80::sub_zone::test_sub_zone
fake_cluster::1.2.3.4:80::canary::false
fake_cluster::1.2.3.4:80::priority::6
fake_cluster::1.2.3.4:80::success_rate::43.2
fake_cluster::1.2.3.4:80::local_origin_success_rate::93.2
)EOF";
  EXPECT_EQ(expected_text, response2.toString());
}

TEST_P(AdminInstanceTest, GetRequest) {
  EXPECT_CALL(server_.options_, toCommandLineOptions()).WillRepeatedly(Invoke([] {
    Server::CommandLineOptionsPtr command_line_options =
        std::make_unique<envoy::admin::v2alpha::CommandLineOptions>();
    command_line_options->set_restart_epoch(2);
    command_line_options->set_service_cluster("cluster");
    return command_line_options;
  }));
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));
  ON_CALL(server_.hot_restart_, version()).WillByDefault(Return("foo_version"));

  {
    Http::HeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v2alpha::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v2alpha::ServerInfo::LIVE);
    EXPECT_EQ(server_info_proto.hot_restart_version(), "foo_version");
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  {
    Http::HeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v2alpha::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v2alpha::ServerInfo::PRE_INITIALIZING);
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  Http::HeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
  envoy::admin::v2alpha::ServerInfo server_info_proto;
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("application/json"));

  // We only test that it parses as the proto and that some fields are correct, since
  // values such as timestamps + Envoy version are tricky to test for.
  TestUtility::loadFromJson(body, server_info_proto);
  EXPECT_EQ(server_info_proto.state(), envoy::admin::v2alpha::ServerInfo::INITIALIZING);
  EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
  EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
}

TEST_P(AdminInstanceTest, GetReadyRequest) {
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));

  {
    Http::HeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "LIVE\n");
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("text/plain"));
  }

  {
    Http::HeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::ServiceUnavailable,
              admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "PRE_INITIALIZING\n");
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("text/plain"));
  }

  Http::HeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            admin_.request("/ready", "GET", response_headers, body));
  EXPECT_EQ(body, "INITIALIZING\n");
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("text/plain"));
}

TEST_P(AdminInstanceTest, GetRequestJson) {
  Http::HeaderMapImpl response_headers;
  std::string body;
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats?format=json", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("{\"stats\":["));
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("application/json"));
}

TEST_P(AdminInstanceTest, RecentLookups) {
  Http::HeaderMapImpl response_headers;
  std::string body;

  // Recent lookup tracking is disabled by default.
  EXPECT_EQ(Http::Code::OK, admin_.request("/stats/recentlookups", "GET", response_headers, body));
  EXPECT_THAT(body, HasSubstr("Lookup tracking is not enabled"));
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("text/plain"));

  // We can't test RecentLookups in admin unit tests as it doesn't work with a
  // fake symbol table. However we cover this solidly in integration tests.
}

TEST_P(AdminInstanceTest, PostRequest) {
  Http::HeaderMapImpl response_headers;
  std::string body;
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::OK,
                           admin_.request("/healthcheck/fail", "POST", response_headers, body)));
  EXPECT_EQ(body, "OK\n");
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("text/plain"));
}

class HistogramWrapper {
public:
  HistogramWrapper() : histogram_(hist_alloc()) {}

  ~HistogramWrapper() { hist_free(histogram_); }

  const histogram_t* getHistogram() { return histogram_; }

  void setHistogramValues(const std::vector<uint64_t>& values) {
    for (uint64_t value : values) {
      hist_insert_intscale(histogram_, value, 0, 1);
    }
  }

  void setHistogramValuesWithCounts(const std::vector<std::pair<uint64_t, uint64_t>>& values) {
    for (std::pair<uint64_t, uint64_t> cv : values) {
      hist_insert_intscale(histogram_, cv.first, 0, cv.second);
    }
  }

private:
  histogram_t* histogram_;
};

class PrometheusStatsFormatterTest : public testing::Test {
protected:
  PrometheusStatsFormatterTest()
      : symbol_table_(Stats::SymbolTableCreator::makeSymbolTable()), alloc_(*symbol_table_) {}

  void addCounter(const std::string& name, std::vector<Stats::Tag> cluster_tags) {
    Stats::StatNameManagedStorage storage(name, *symbol_table_);
    counters_.push_back(alloc_.makeCounter(storage.statName(), name, cluster_tags));
  }

  void addGauge(const std::string& name, std::vector<Stats::Tag> cluster_tags) {
    Stats::StatNameManagedStorage storage(name, *symbol_table_);
    gauges_.push_back(alloc_.makeGauge(storage.statName(), name, cluster_tags,
                                       Stats::Gauge::ImportMode::Accumulate));
  }

  void addHistogram(const Stats::ParentHistogramSharedPtr histogram) {
    histograms_.push_back(histogram);
  }

  using MockHistogramSharedPtr = Stats::RefcountPtr<NiceMock<Stats::MockParentHistogram>>;
  MockHistogramSharedPtr makeHistogram() {
    return MockHistogramSharedPtr(new NiceMock<Stats::MockParentHistogram>());
  }

  Stats::SymbolTablePtr symbol_table_;
  Stats::AllocatorImpl alloc_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
};

TEST_F(PrometheusStatsFormatterTest, MetricName) {
  std::string raw = "vulture.eats-liver";
  std::string expected = "envoy_vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricName) {
  std::string raw = "An.artist.plays-violin@019street";
  std::string expected = "envoy_An_artist_plays_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, SanitizeMetricNameDigitFirst) {
  std::string raw = "3.artists.play-violin@019street";
  std::string expected = "envoy_3_artists_play_violin_019street";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, FormattedTags) {
  std::vector<Stats::Tag> tags;
  Stats::Tag tag1 = {"a.tag-name", "a.tag-value"};
  Stats::Tag tag2 = {"another_tag_name", "another_tag-value"};
  tags.push_back(tag1);
  tags.push_back(tag2);
  std::string expected = "a_tag_name=\"a.tag-value\",another_tag_name=\"another_tag-value\"";
  auto actual = PrometheusStatsFormatter::formattedTags(tags);
  EXPECT_EQ(expected, actual);
}

TEST_F(PrometheusStatsFormatterTest, MetricNameCollison) {

  // Create two counters and two gauges with each pair having the same name,
  // but having different tag names and values.
  //`statsAsPrometheus()` should return two implying it found two unique stat names

  addCounter("cluster.test_cluster_1.upstream_cx_total", {{"a.tag-name", "a.tag-value"}});
  addCounter("cluster.test_cluster_1.upstream_cx_total",
             {{"another_tag_name", "another_tag-value"}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{"another_tag_name_3", "another_tag_3-value"}});
  addGauge("cluster.test_cluster_2.upstream_cx_total",
           {{"another_tag_name_4", "another_tag_4-value"}});

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(2UL, size);
}

TEST_F(PrometheusStatsFormatterTest, UniqueMetricName) {

  // Create two counters and two gauges, all with unique names.
  // statsAsPrometheus() should return four implying it found
  // four unique stat names.

  addCounter("cluster.test_cluster_1.upstream_cx_total", {{"a.tag-name", "a.tag-value"}});
  addCounter("cluster.test_cluster_2.upstream_cx_total",
             {{"another_tag_name", "another_tag-value"}});
  addGauge("cluster.test_cluster_3.upstream_cx_total",
           {{"another_tag_name_3", "another_tag_3-value"}});
  addGauge("cluster.test_cluster_4.upstream_cx_total",
           {{"another_tag_name_4", "another_tag_4-value"}});

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(4UL, size);
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithNoValuesAndNoTags) {
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(std::vector<uint64_t>(0));
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram();
  histogram->name_ = "histogram1";
  histogram->used_ = true;
  ON_CALL(*histogram, cumulativeStatistics())
      .WillByDefault(testing::ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 0
envoy_histogram1_bucket{le="1"} 0
envoy_histogram1_bucket{le="5"} 0
envoy_histogram1_bucket{le="10"} 0
envoy_histogram1_bucket{le="25"} 0
envoy_histogram1_bucket{le="50"} 0
envoy_histogram1_bucket{le="100"} 0
envoy_histogram1_bucket{le="250"} 0
envoy_histogram1_bucket{le="500"} 0
envoy_histogram1_bucket{le="1000"} 0
envoy_histogram1_bucket{le="2500"} 0
envoy_histogram1_bucket{le="5000"} 0
envoy_histogram1_bucket{le="10000"} 0
envoy_histogram1_bucket{le="30000"} 0
envoy_histogram1_bucket{le="60000"} 0
envoy_histogram1_bucket{le="300000"} 0
envoy_histogram1_bucket{le="600000"} 0
envoy_histogram1_bucket{le="1800000"} 0
envoy_histogram1_bucket{le="3600000"} 0
envoy_histogram1_bucket{le="+Inf"} 0
envoy_histogram1_sum{} 0
envoy_histogram1_count{} 0
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, HistogramWithHighCounts) {
  HistogramWrapper h1_cumulative;

  // Force large counts to prove that the +Inf bucket doesn't overflow to scientific notation.
  h1_cumulative.setHistogramValuesWithCounts(std::vector<std::pair<uint64_t, uint64_t>>({
      {1, 100000},
      {100, 1000000},
      {1000, 100000000},
  }));

  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram = makeHistogram();
  histogram->name_ = "histogram1";
  histogram->used_ = true;
  ON_CALL(*histogram, cumulativeStatistics())
      .WillByDefault(testing::ReturnRef(h1_cumulative_statistics));

  addHistogram(histogram);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_histogram1 histogram
envoy_histogram1_bucket{le="0.5"} 0
envoy_histogram1_bucket{le="1"} 0
envoy_histogram1_bucket{le="5"} 100000
envoy_histogram1_bucket{le="10"} 100000
envoy_histogram1_bucket{le="25"} 100000
envoy_histogram1_bucket{le="50"} 100000
envoy_histogram1_bucket{le="100"} 100000
envoy_histogram1_bucket{le="250"} 1100000
envoy_histogram1_bucket{le="500"} 1100000
envoy_histogram1_bucket{le="1000"} 1100000
envoy_histogram1_bucket{le="2500"} 101100000
envoy_histogram1_bucket{le="5000"} 101100000
envoy_histogram1_bucket{le="10000"} 101100000
envoy_histogram1_bucket{le="30000"} 101100000
envoy_histogram1_bucket{le="60000"} 101100000
envoy_histogram1_bucket{le="300000"} 101100000
envoy_histogram1_bucket{le="600000"} 101100000
envoy_histogram1_bucket{le="1800000"} 101100000
envoy_histogram1_bucket{le="3600000"} 101100000
envoy_histogram1_bucket{le="+Inf"} 101100000
envoy_histogram1_sum{} 105105105000
envoy_histogram1_count{} 101100000
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithAllMetricTypes) {
  addCounter("cluster.test_1.upstream_cx_total", {{"a.tag-name", "a.tag-value"}});
  addCounter("cluster.test_2.upstream_cx_total", {{"another_tag_name", "another_tag-value"}});
  addGauge("cluster.test_3.upstream_cx_total", {{"another_tag_name_3", "another_tag_3-value"}});
  addGauge("cluster.test_4.upstream_cx_total", {{"another_tag_name_4", "another_tag_4-value"}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram();
  histogram1->name_ = "cluster.test_1.upstream_rq_time";
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->used_ = true;
  histogram1->setTags({Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}});
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics())
      .WillOnce(testing::ReturnRef(h1_cumulative_statistics));

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          false, absl::nullopt);
  EXPECT_EQ(5UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
# TYPE envoy_cluster_test_2_upstream_cx_total counter
envoy_cluster_test_2_upstream_cx_total{another_tag_name="another_tag-value"} 0
# TYPE envoy_cluster_test_3_upstream_cx_total gauge
envoy_cluster_test_3_upstream_cx_total{another_tag_name_3="another_tag_3-value"} 0
# TYPE envoy_cluster_test_4_upstream_cx_total gauge
envoy_cluster_test_4_upstream_cx_total{another_tag_name_4="another_tag_4-value"} 0
# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 1
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 2
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 4
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5532
envoy_cluster_test_1_upstream_rq_time_count{key1="value1",key2="value2"} 7
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithUsedOnly) {
  addCounter("cluster.test_1.upstream_cx_total", {{"a.tag-name", "a.tag-value"}});
  addCounter("cluster.test_2.upstream_cx_total", {{"another_tag_name", "another_tag-value"}});
  addGauge("cluster.test_3.upstream_cx_total", {{"another_tag_name_3", "another_tag_3-value"}});
  addGauge("cluster.test_4.upstream_cx_total", {{"another_tag_name_4", "another_tag_4-value"}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram();
  histogram1->name_ = "cluster.test_1.upstream_rq_time";
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->used_ = true;
  histogram1->setTags({Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}});
  addHistogram(histogram1);
  EXPECT_CALL(*histogram1, cumulativeStatistics())
      .WillOnce(testing::ReturnRef(h1_cumulative_statistics));

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_, response,
                                                          true, absl::nullopt);
  EXPECT_EQ(1UL, size);

  const std::string expected_output = R"EOF(# TYPE envoy_cluster_test_1_upstream_rq_time histogram
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="0.5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10"} 0
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="25"} 1
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="50"} 2
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="100"} 4
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="250"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="2500"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="5000"} 6
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="10000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="30000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="60000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="300000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="1800000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="3600000"} 7
envoy_cluster_test_1_upstream_rq_time_bucket{key1="value1",key2="value2",le="+Inf"} 7
envoy_cluster_test_1_upstream_rq_time_sum{key1="value1",key2="value2"} 5532
envoy_cluster_test_1_upstream_rq_time_count{key1="value1",key2="value2"} 7
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

TEST_F(PrometheusStatsFormatterTest, OutputWithUsedOnlyHistogram) {
  const std::vector<uint64_t> h1_values = {};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram();
  histogram1->name_ = "cluster.test_1.upstream_rq_time";
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->used_ = false;
  histogram1->setTags({Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}});
  addHistogram(histogram1);

  {
    const bool used_only = true;
    EXPECT_CALL(*histogram1, cumulativeStatistics()).Times(0);

    Buffer::OwnedImpl response;
    auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_,
                                                            response, used_only, absl::nullopt);
    EXPECT_EQ(0UL, size);
  }

  {
    const bool used_only = false;
    EXPECT_CALL(*histogram1, cumulativeStatistics())
        .WillOnce(testing::ReturnRef(h1_cumulative_statistics));

    Buffer::OwnedImpl response;
    auto size = PrometheusStatsFormatter::statsAsPrometheus(counters_, gauges_, histograms_,
                                                            response, used_only, absl::nullopt);
    EXPECT_EQ(1UL, size);
  }
}

TEST_F(PrometheusStatsFormatterTest, OutputWithRegexp) {
  addCounter("cluster.test_1.upstream_cx_total", {{"a.tag-name", "a.tag-value"}});
  addCounter("cluster.test_2.upstream_cx_total", {{"another_tag_name", "another_tag-value"}});
  addGauge("cluster.test_3.upstream_cx_total", {{"another_tag_name_3", "another_tag_3-value"}});
  addGauge("cluster.test_4.upstream_cx_total", {{"another_tag_name_4", "another_tag_4-value"}});

  const std::vector<uint64_t> h1_values = {50, 20, 30, 70, 100, 5000, 200};
  HistogramWrapper h1_cumulative;
  h1_cumulative.setHistogramValues(h1_values);
  Stats::HistogramStatisticsImpl h1_cumulative_statistics(h1_cumulative.getHistogram());

  auto histogram1 = makeHistogram();
  histogram1->name_ = "cluster.test_1.upstream_rq_time";
  histogram1->unit_ = Stats::Histogram::Unit::Milliseconds;
  histogram1->setTags({Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}});
  addHistogram(histogram1);

  Buffer::OwnedImpl response;
  auto size = PrometheusStatsFormatter::statsAsPrometheus(
      counters_, gauges_, histograms_, response, false,
      absl::optional<std::regex>{std::regex("cluster.test_1.upstream_cx_total")});
  EXPECT_EQ(1UL, size);

  const std::string expected_output =
      R"EOF(# TYPE envoy_cluster_test_1_upstream_cx_total counter
envoy_cluster_test_1_upstream_cx_total{a_tag_name="a.tag-value"} 0
)EOF";

  EXPECT_EQ(expected_output, response.toString());
}

} // namespace Server
} // namespace Envoy
