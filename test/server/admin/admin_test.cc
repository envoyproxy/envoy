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

#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

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
  EXPECT_EQ(admin_.pathWithEscapedSlashesAction(),
            envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::KEEP_UNCHANGED);
  EXPECT_NE(nullptr, admin_.scopedRouteConfigProvider());
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().connectionInfoProvider().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminAddress) {
  const std::string address_out_path = TestEnvironment::temporaryPath("admin.address");
  AdminImpl admin_address_out_path(cpu_profile_path_, server_, false);
  std::list<AccessLog::InstanceSharedPtr> access_logs;
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, "/dev/null"};
  access_logs.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      file_info, {}, Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(),
      server_.accessLogManager()));
  EXPECT_LOG_CONTAINS("info", "admin address:",
                      admin_address_out_path.startHttpListener(
                          access_logs, address_out_path,
                          Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                          listener_scope_.createScope("listener.admin.")));
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  const std::string bad_path =
      TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(cpu_profile_path_, server_, false);
  std::list<AccessLog::InstanceSharedPtr> access_logs;
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, "/dev/null"};
  access_logs.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      file_info, {}, Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(),
      server_.accessLogManager()));
  EXPECT_LOG_CONTAINS(
      "critical", "cannot open admin address output file " + bad_path + " for writing.",
      admin_bad_address_out_path.startHttpListener(
          access_logs, bad_path, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
          listener_scope_.createScope("listener.admin.")));
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

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
  auto callback = [](absl::string_view, AdminStream&) -> Admin::RequestPtr {
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

  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_THAT(std::string(content_type.getStringView()), testing::HasSubstr("text/html"));
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0, 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0, 0));
}

TEST_P(AdminInstanceTest, HelpUsesFormForMutations) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const std::string logging_action = "<form action='logging' method='post'";
  const std::string stats_href = "<a href='stats'";
  EXPECT_NE(-1, response.search(logging_action.data(), logging_action.size(), 0, 0));
  EXPECT_NE(-1, response.search(stats_href.data(), stats_href.size(), 0, 0));
}

class AdminTestingPeer {
public:
  AdminTestingPeer(AdminImpl& admin)
      : admin_(admin), server_(admin.server_), listener_scope_(server_.stats().createScope("")) {}
  AdminImpl::NullRouteConfigProvider& routeConfigProvider() { return route_config_provider_; }
  AdminImpl::NullScopedRouteConfigProvider& scopedRouteConfigProvider() {
    return scoped_route_config_provider_;
  }
  AdminImpl::NullOverloadManager& overloadManager() { return admin_.null_overload_manager_; }
  AdminImpl::NullOverloadManager::OverloadState& overloadState() { return overload_state_; }
  AdminImpl::AdminListenSocketFactory& socketFactory() { return socket_factory_; }
  AdminImpl::AdminListener& listener() { return listener_; }

private:
  AdminImpl& admin_;
  Server::Instance& server_;
  AdminImpl::NullRouteConfigProvider route_config_provider_{server_.timeSource()};
  AdminImpl::NullScopedRouteConfigProvider scoped_route_config_provider_{server_.timeSource()};
  AdminImpl::NullOverloadManager::OverloadState overload_state_{server_.dispatcher()};
  AdminImpl::AdminListenSocketFactory socket_factory_{nullptr};
  Stats::ScopeSharedPtr listener_scope_;
  AdminImpl::AdminListener listener_{admin_, std::move(listener_scope_)};
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
  peer.routeConfigProvider().onConfigUpdate();

  peer.scopedRouteConfigProvider().lastUpdated();
  peer.scopedRouteConfigProvider().getConfigProto();
  peer.scopedRouteConfigProvider().getConfigVersion();
  peer.scopedRouteConfigProvider().getConfig();
  peer.scopedRouteConfigProvider().apiType();
  peer.scopedRouteConfigProvider().getConfigProtos();

  auto overload_name = Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections;
  peer.overloadState().tryAllocateResource(overload_name, 0);
  peer.overloadState().tryDeallocateResource(overload_name, 0);
  peer.overloadState().isResourceMonitorEnabled(overload_name);

  peer.overloadManager().scaledTimerFactory();

  peer.socketFactory().clone();
  peer.socketFactory().closeAllSockets();
  peer.socketFactory().doFinalPreWorkerInit();

  peer.listener().name();
  peer.listener().udpListenerConfig();
  peer.listener().direction();
  peer.listener().tcpBacklogSize();
}

} // namespace Server
} // namespace Envoy
