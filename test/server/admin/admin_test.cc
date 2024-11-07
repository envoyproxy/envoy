#include <algorithm>
#include <fstream>
#include <memory>
#include <regex>
#include <vector>

#include "envoy/json/json_object.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"
#include "source/server/admin/stats_request.h"
#include "source/server/configuration_impl.h"
#include "source/server/null_overload_manager.h"

#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StartsWith;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, MutatesErrorWithGet) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;
  const std::string path("/healthcheck/fail");
  // TODO(jmarantz): the call to getCallback should be made to fail, but as an interim we will
  // just issue a warning, so that scripts using curl GET commands to mutate state can be fixed.
  EXPECT_LOG_CONTAINS("error",
                      "admin path \"" + path + "\" mutates state, method=GET rather than POST",
                      EXPECT_EQ(Http::Code::MethodNotAllowed, getCallback(path, header_map, data)));
}

TEST_P(AdminInstanceTest, Getters) {
  EXPECT_EQ(&admin_.mutableSocket(), &admin_.socket());
  EXPECT_EQ(1, admin_.concurrency());
  EXPECT_FALSE(admin_.preserveExternalRequestId());
  EXPECT_EQ(nullptr, admin_.tracer());
  EXPECT_FALSE(admin_.streamErrorOnInvalidHttpMessaging());
  EXPECT_FALSE(admin_.schemeToSet().has_value());
  EXPECT_FALSE(admin_.shouldSchemeMatchUpstream());
  EXPECT_EQ(admin_.pathWithEscapedSlashesAction(),
            envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::KEEP_UNCHANGED);
  EXPECT_NE(nullptr, admin_.scopedRouteConfigProvider());
#ifdef ENVOY_ENABLE_UHV
  // In UHV mode there is always UHV factory
  EXPECT_NE(nullptr, admin_.makeHeaderValidator(Http::Protocol::Http11));
#else
  // In non UHV mode, header validator can not be created
  EXPECT_EQ(nullptr, admin_.makeHeaderValidator(Http::Protocol::Http11));
#endif
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(server_.options_.admin_address_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().connectionInfoProvider().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminAddress) {
  server_.options_.admin_address_path_ = TestEnvironment::temporaryPath("admin.address");
  AdminImpl admin_address_out_path(cpu_profile_path_, server_, false);
  std::list<AccessLog::InstanceSharedPtr> access_logs;
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, "/dev/null"};
  access_logs.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      file_info, {}, *Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter(),
      server_.accessLogManager()));
  EXPECT_LOG_CONTAINS(
      "info", "admin address:",
      admin_address_out_path.startHttpListener(
          access_logs, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr));
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  server_.options_.admin_address_path_ =
      TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(cpu_profile_path_, server_, false);
  std::list<AccessLog::InstanceSharedPtr> access_logs;
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, "/dev/null"};
  access_logs.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      file_info, {}, *Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter(),
      server_.accessLogManager()));
  EXPECT_LOG_CONTAINS(
      "critical",
      "cannot open admin address output file " + server_.options_.admin_address_path_ +
          " for writing.",
      admin_bad_address_out_path.startHttpListener(
          access_logs, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr));
  EXPECT_FALSE(std::ifstream(server_.options_.admin_address_path_));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [](Http::HeaderMap&, Buffer::Instance&, AdminStream&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // Test removable handler.
  EXPECT_NO_LOGS(EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true, false)));
  Http::TestResponseHeaderMapImpl header_map;
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

TEST_P(AdminInstanceTest, Help) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/help", header_map, response));
  const std::string expected = R"EOF(admin commands are:
  /: Admin home page
  /allocprofiler (POST): enable/disable the allocation profiler (if supported)
      enable: enable/disable the allocation profiler; One of (y, n)
  /certs: print certs on machine
  /clusters: upstream cluster status
  /config_dump: dump current Envoy configs (experimental)
      resource: The resource to dump
      mask: The mask to apply. When both resource and mask are specified, the mask is applied to every element in the desired repeated field so that only a subset of fields are returned. The mask is parsed as a ProtobufWkt::FieldMask
      name_regex: Dump only the currently loaded configurations whose names match the specified regex. Can be used with both resource and mask query parameters.
      include_eds: Dump currently loaded configuration including EDS. See the response definition for more information
  /contention: dump current Envoy mutex contention stats (if enabled)
  /cpuprofiler (POST): enable/disable the CPU profiler
      enable: enables the CPU profiler; One of (y, n)
  /drain_listeners (POST): drain listeners
      graceful: When draining listeners, enter a graceful drain period prior to closing listeners. This behaviour and duration is configurable via server options or CLI
      skip_exit: When draining listeners, do not exit after the drain period. This must be used with graceful
      inboundonly: Drains all inbound listeners. traffic_direction field in envoy_v3_api_msg_config.listener.v3.Listener is used to determine whether a listener is inbound or outbound.
  /healthcheck/fail (POST): cause the server to fail health checks
  /healthcheck/ok (POST): cause the server to pass health checks
  /heap_dump: dump current Envoy heap (if supported)
  /heapprofiler (POST): enable/disable the heap profiler
      enable: enable/disable the heap profiler; One of (y, n)
  /help: print out list of admin commands
  /hot_restart_version: print the hot restart compatibility version
  /init_dump: dump current Envoy init manager information (experimental)
      mask: The desired component to dump unready targets. The mask is parsed as a ProtobufWkt::FieldMask. For example, get the unready targets of all listeners with /init_dump?mask=listener`
  /listeners: print listener info
      format: File format to use; One of (text, json)
  /logging (POST): query/change logging levels
      paths: Change multiple logging levels by setting to <logger_name1>:<desired_level1>,<logger_name2>:<desired_level2>. If fine grain logging is enabled, use __FILE__ or a glob experision as the logger name. For example, source/common*:warning
      level: desired logging level, this will change all loggers's level; One of (, trace, debug, info, warning, error, critical, off)
  /memory: print current allocation/heap usage
  /quitquitquit (POST): exit the server
  /ready: print server state, return 200 if LIVE, otherwise return 503
  /reopen_logs (POST): reopen access logs
  /reset_counters (POST): reset all counters to zero
  /runtime: print runtime values
  /runtime_modify (POST): Adds or modifies runtime values as passed in query parameters. To delete a previously added key, use an empty string as the value. Note that deletion only applies to overrides added via this endpoint; values loaded from disk can be modified via override but not deleted. E.g. ?key1=value1&key2=value2...
  /server_info: print server version/status information
  /stats: print server stats
      usedonly: Only include stats that have been written by system since restart
      filter: Regular expression (Google re2) for filtering stats
      format: Format to use; One of (html, active-html, text, json)
      type: Stat types to include.; One of (All, Counters, Histograms, Gauges, TextReadouts)
      histogram_buckets: Histogram bucket display mode; One of (cumulative, disjoint, detailed, summary)
  /stats/prometheus: print server stats in prometheus format
      usedonly: Only include stats that have been written by system since restart
      text_readouts: Render text_readouts as new gaugues with value 0 (increases Prometheus data size)
      filter: Regular expression (Google re2) for filtering stats
      histogram_buckets: Histogram bucket display mode; One of (cumulative, summary)
  /stats/recentlookups: Show recent stat-name lookups
  /stats/recentlookups/clear (POST): clear list of stat-name lookups and counter
  /stats/recentlookups/disable (POST): disable recording of reset stat-name lookup names
  /stats/recentlookups/enable (POST): enable recording of reset stat-name lookup names
)EOF";
  EXPECT_EQ(expected, response.toString());
}

class ChunkedHandler : public Admin::Request {
public:
  Http::Code start(Http::ResponseHeaderMap&) override { return Http::Code::OK; }

  bool nextChunk(Buffer::Instance& response) override {
    response.add("Text ");
    return ++count_ < 3;
  }

private:
  uint32_t count_{0};
};

TEST_P(AdminInstanceTest, CustomChunkedHandler) {
  auto callback = [](AdminStream&) -> Admin::RequestPtr {
    Admin::RequestPtr handler = Admin::RequestPtr(new ChunkedHandler);
    return handler;
  };

  // Test removable handler.
  EXPECT_NO_LOGS(
      EXPECT_TRUE(admin_.addStreamingHandler("/foo/bar", "hello", callback, true, false)));
  Http::TestResponseHeaderMapImpl header_map;
  {
    Buffer::OwnedImpl response;
    EXPECT_EQ(Http::Code::OK, getCallback("/foo/bar", header_map, response));
    EXPECT_EQ("Text Text Text ", response.toString());
  }

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::NotFound, getCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addStreamingHandler("/foo/bar", "hello", callback, false, false));
  EXPECT_EQ(Http::Code::OK, getCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addStreamingHandler("/foo/bar", "hello", callback, false, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  {
    Buffer::OwnedImpl response;
    EXPECT_EQ(Http::Code::OK, getCallback("/foo/bar", header_map, response));
    EXPECT_EQ("Text Text Text ", response.toString());
  }
}

TEST_P(AdminInstanceTest, StatsWithMultipleChunks) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  Stats::Store& store = server_.stats();

  // Cover the case where multiple chunks are emitted by making a large number
  // of stats with long names, based on the default chunk size. The actual
  // chunk size can be changed in the unit test for StatsRequest, but it can't
  // easily be changed from this test. This covers a bug fix due to
  // AdminImpl::runRunCallback not draining the buffer after each chunk, which
  // it is not required to do. This test ensures that StatsRequest::nextChunk
  // writes up to StatsRequest::DefaultChunkSize *additional* bytes on each
  // call.
  const std::string prefix(1000, 'a');
  uint32_t expected_size = 0;

  // Declare enough counters so that we are sure to exceed the chunk size.
  const uint32_t n = (StatsRequest::DefaultChunkSize + prefix.size() / 2) / prefix.size() + 1;
  for (uint32_t i = 0; i <= n; ++i) {
    const std::string name = absl::StrCat(prefix, i);
    store.counterFromString(name);
    expected_size += name.size() + strlen(": 0\n");
  }
  EXPECT_EQ(Http::Code::OK, getCallback("/stats", header_map, response));
  EXPECT_LT(expected_size, response.length());
  EXPECT_LT(StatsRequest::DefaultChunkSize, response.length());
  EXPECT_THAT(response.toString(), StartsWith(absl::StrCat(prefix, "0: 0\n", prefix)));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [](Http::HeaderMap&, Buffer::Instance&, AdminStream&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/foo<script>alert('hi')</script>\" contains invalid character '<'",
                      EXPECT_FALSE(admin_.addHandler("/foo<script>alert('hi')</script>", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [](Http::HeaderMap&, Buffer::Instance&, AdminStream&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/bar?queryShouldNotBeInPrefix\" contains invalid character '?'",
                      EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello",
                                                     callback, true, false)));
}

class AdminTestingPeer {
public:
  AdminTestingPeer(AdminImpl& admin)
      : admin_(admin), server_(admin.server_), listener_scope_(server_.stats().createScope("")) {}
  AdminImpl::NullRouteConfigProvider& routeConfigProvider() { return route_config_provider_; }
  AdminImpl::NullScopedRouteConfigProvider& scopedRouteConfigProvider() {
    return scoped_route_config_provider_;
  }
  OverloadManager& overloadManager() { return admin_.null_overload_manager_; }
  NullOverloadManager::OverloadState& overloadState() { return overload_state_; }
  AdminImpl::AdminListenSocketFactory& socketFactory() { return socket_factory_; }
  AdminImpl::AdminListener& listener() { return listener_; }

private:
  AdminImpl& admin_;
  Server::Instance& server_;
  AdminImpl::NullRouteConfigProvider route_config_provider_{server_.timeSource()};
  AdminImpl::NullScopedRouteConfigProvider scoped_route_config_provider_{server_.timeSource()};
  NullOverloadManager::OverloadState overload_state_{server_.dispatcher(), false};
  AdminImpl::AdminListenSocketFactory socket_factory_{nullptr};
  Stats::ScopeSharedPtr listener_scope_;
  AdminImpl::AdminListener listener_{admin_, *listener_scope_};
};

// Covers override methods for admin-specific implementations of classes in
// admin.h, reducing a major source of reduced coverage-percent expectations in
// source/server/admin. There remain a few uncovered lines that are somewhat
// harder to construct tests for.
TEST_P(AdminInstanceTest, Overrides) {
  admin_.http1Settings();
  admin_.originalIpDetectionExtensions();

  AdminTestingPeer peer(admin_);

  peer.routeConfigProvider().config();
  peer.routeConfigProvider().configInfo();
  peer.routeConfigProvider().lastUpdated();
  ASSERT_TRUE(peer.routeConfigProvider().onConfigUpdate().ok());

  peer.scopedRouteConfigProvider().lastUpdated();
  peer.scopedRouteConfigProvider().getConfig();
  peer.scopedRouteConfigProvider().apiType();
  peer.scopedRouteConfigProvider().getConfigProtos();

  auto overload_name = Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections;
  peer.overloadState().tryAllocateResource(overload_name, 0);
  peer.overloadState().tryDeallocateResource(overload_name, 0);
  peer.overloadState().isResourceMonitorEnabled(overload_name);
  peer.overloadState().getProactiveResourceMonitorForTest(overload_name);

  peer.overloadManager().scaledTimerFactory();

  peer.socketFactory().clone();
  peer.socketFactory().closeAllSockets();
  ASSERT_TRUE(peer.socketFactory().doFinalPreWorkerInit().ok());

  peer.listener().name();
  peer.listener().udpListenerConfig();
  peer.listener().listenerInfo()->direction();
  peer.listener().tcpBacklogSize();
}

} // namespace Server
} // namespace Envoy
