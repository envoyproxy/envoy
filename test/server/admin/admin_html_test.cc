#include "test/server/admin/admin_instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [](Http::HeaderMap&, Buffer::Instance&, AdminStream&) -> Http::Code {
    return Http::Code::Accepted;
  };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string planets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", planets, callback, true, false));

  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_THAT(std::string(content_type.getStringView()), HasSubstr("text/html"));
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0, 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0, 0));
}

TEST_P(AdminInstanceTest, HelpUsesPostForMutations) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  EXPECT_THAT(response.toString(), HasSubstr("<form action='logging' method='post'"));
  EXPECT_THAT(response.toString(), HasSubstr("<form action='stats' method='get'"));
}

} // namespace Server
} // namespace Envoy
