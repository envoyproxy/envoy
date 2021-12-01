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
  std::string address_out_path = TestEnvironment::temporaryPath("admin.address");
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
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
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

} // namespace Server
} // namespace Envoy
