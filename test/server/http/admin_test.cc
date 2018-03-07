#include <fstream>
#include <unordered_map>

#include "envoy/json/json_object.h"
#include "envoy/runtime/runtime.h"

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/profiler/profiler.h"

#include "server/http/admin.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {

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

INSTANTIATE_TEST_CASE_P(IpVersions, AdminFilterTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

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
               listener_scope_.createScope("listener.admin.")) {
    EXPECT_EQ(std::chrono::milliseconds(100), admin_.drainTimeout());
    admin_.tracingStats().random_sampling_.inc();
    EXPECT_TRUE(admin_.setCurrentClientCertDetails().empty());
  }

  std::string address_out_path_;
  std::string cpu_profile_path_;
  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AdminInstanceTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
// Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
// a real profiler linked in (successful call to startProfiler). startProfiler
// requies tcmalloc.
#ifdef TCMALLOC

TEST_P(AdminInstanceTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  Http::HeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

#endif

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path("/dev/null",
                                   TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   "", Network::Test::getCanonicalLoopbackAddress(GetParam()),
                                   server_, listener_scope_.createScope("listener.admin."));
  Http::HeaderMapImpl header_map;
  admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map, data);
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
  AdminImpl admin_bad_address_out_path("/dev/null", cpu_profile_path_, bad_path,
                                       Network::Test::getCanonicalLoopbackAddress(GetParam()),
                                       server_, listener_scope_.createScope("listener.admin."));
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // Test removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true, false));
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::NotFound, admin_.runCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, false, false));
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addHandler("/foo/bar", "hello", callback, false, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::Accepted, admin_.runCallback("/foo/bar", header_map, response));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_FALSE(
      admin_.addHandler("/foo<script>alert('hi')</script>", "hello", callback, true, false));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };
  EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello", callback, true, false));
}

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [&](const std::string&, Http::HeaderMap&, Buffer::Instance&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string planets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", planets, callback, true, false));

  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/", header_map, response));
  Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_TRUE(content_type.find("text/html")) << content_type.c_str();
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0));
}

TEST_P(AdminInstanceTest, HelpUsesFormForMutations) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/", header_map, response));
  const std::string logging_action = "<form action='/logging' method='post'";
  const std::string stats_href = "<a href='/stats'";
  EXPECT_NE(-1, response.search(logging_action.data(), logging_action.size(), 0));
  EXPECT_NE(-1, response.search(stats_href.data(), stats_href.size(), 0));
}

TEST_P(AdminInstanceTest, Runtime) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  std::unordered_map<std::string, const Runtime::Snapshot::Entry> entries{
      {"string_key", {"foo", {}}}, {"int_key", {"1", {1}}}, {"other_key", {"bar", {}}}};
  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;

  EXPECT_CALL(snapshot, getAll()).WillRepeatedly(testing::ReturnRef(entries));
  EXPECT_CALL(loader, snapshot()).WillRepeatedly(testing::ReturnPointee(&snapshot));
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/runtime", header_map, response));
  EXPECT_EQ("int_key: 1\nother_key: bar\nstring_key: foo\n", TestUtility::bufferToString(response));
}

TEST_P(AdminInstanceTest, RuntimeJSON) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  std::unordered_map<std::string, const Runtime::Snapshot::Entry> entries{
      {"string_key", {"foo", {}}}, {"int_key", {"1", {1}}}, {"other_key", {"bar", {}}}};
  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;

  EXPECT_CALL(snapshot, getAll()).WillRepeatedly(testing::ReturnRef(entries));
  EXPECT_CALL(loader, snapshot()).WillRepeatedly(testing::ReturnPointee(&snapshot));
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  EXPECT_EQ(Http::Code::OK, admin_.runCallback("/runtime?format=json", header_map, response));

  std::string output = TestUtility::bufferToString(response);
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(output);

  EXPECT_TRUE(json->hasObject("runtime"));
  std::vector<Json::ObjectSharedPtr> pairs = json->getObjectArray("runtime");
  EXPECT_EQ(3, pairs.size());

  Json::ObjectSharedPtr pair = pairs[0];
  EXPECT_EQ("int_key", pair->getString("name", ""));
  EXPECT_EQ(1, pair->getInteger("value", -1));

  pair = pairs[1];
  EXPECT_EQ("other_key", pair->getString("name", ""));
  EXPECT_EQ("bar", pair->getString("value", ""));

  pair = pairs[2];
  EXPECT_EQ("string_key", pair->getString("name", ""));
  EXPECT_EQ("foo", pair->getString("value", ""));
}

TEST_P(AdminInstanceTest, RuntimeBadFormat) {
  Http::HeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  std::unordered_map<std::string, const Runtime::Snapshot::Entry> entries;
  Runtime::MockSnapshot snapshot;
  Runtime::MockLoader loader;

  EXPECT_CALL(snapshot, getAll()).WillRepeatedly(testing::ReturnRef(entries));
  EXPECT_CALL(loader, snapshot()).WillRepeatedly(testing::ReturnPointee(&snapshot));
  EXPECT_CALL(server_, runtime()).WillRepeatedly(testing::ReturnPointee(&loader));

  EXPECT_EQ(Http::Code::BadRequest,
            admin_.runCallback("/runtime?format=foo", header_map, response));
  EXPECT_EQ("usage: /runtime?format=json\n", TestUtility::bufferToString(response));
}

TEST(PrometheusStatsFormatter, MetricName) {
  std::string raw = "vulture.eats-liver";
  std::string expected = "envoy_vulture_eats_liver";
  auto actual = PrometheusStatsFormatter::metricName(raw);
  EXPECT_EQ(expected, actual);
}

TEST(PrometheusStatsFormatter, FormattedTags) {
  std::vector<Stats::Tag> tags;
  Stats::Tag tag1 = {"a.tag-name", "a.tag-value"};
  Stats::Tag tag2 = {"another_tag_name", "another_tag-value"};
  tags.push_back(tag1);
  tags.push_back(tag2);
  std::string expected = "a_tag_name=\"a.tag-value\",another_tag_name=\"another_tag-value\"";
  auto actual = PrometheusStatsFormatter::formattedTags(tags);
  EXPECT_EQ(expected, actual);
}

} // namespace Server
} // namespace Envoy
