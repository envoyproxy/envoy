#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(SessionCodecTest, EncodeDecode) {
  EXPECT_EQ("aGVsbG8=", SessionCodec::encode("hello"));
  EXPECT_EQ("hello", SessionCodec::decode("aGVsbG8="));
  EXPECT_EQ("", SessionCodec::decode(SessionCodec::encode("")));
}

TEST(SessionCodecTest, BuildCompositeSessionId) {
  const std::string id = SessionCodec::buildCompositeSessionId(
      "route1", "user1", {{"backend1", "s1"}, {"backend2", "s2"}});

  EXPECT_THAT(id, testing::StartsWith("route1@" + SessionCodec::encode("user1") + "@"));
  EXPECT_THAT(id, testing::HasSubstr("backend1:" + SessionCodec::encode("s1")));
  EXPECT_THAT(id, testing::HasSubstr("backend2:" + SessionCodec::encode("s2")));
}

TEST(SessionCodecTest, ParseCompositeSessionId) {
  std::string composite = absl::StrCat("route1@", SessionCodec::encode("user1"),
                                       "@backend1:", SessionCodec::encode("s1"),
                                       ",backend2:", SessionCodec::encode("s2"));

  auto result = SessionCodec::parseCompositeSessionId(composite);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->route, "route1");
  EXPECT_EQ(result->subject, "user1");
  EXPECT_THAT(result->backend_sessions,
              UnorderedElementsAre(Pair("backend1", "s1"), Pair("backend2", "s2")));
}

// Test that subjects containing splitter are correctly handled.
TEST(SessionCodecTest, SubjectWithAtSymbol) {
  const std::string subject_with_at = "user@example.com";
  const std::string id = SessionCodec::buildCompositeSessionId("my_route", subject_with_at,
                                                               {{"backend1", "session1"}});

  auto result = SessionCodec::parseCompositeSessionId(id);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->route, "my_route");
  EXPECT_EQ(result->subject, subject_with_at);
  EXPECT_THAT(result->backend_sessions, UnorderedElementsAre(Pair("backend1", "session1")));
}

TEST(SessionCodecTest, ParseInvalidCustomFormat) {
  const std::vector<std::string> invalid_inputs = {
      "invalid", "no_backends@user",
      "route@user@backend",  // Missing colon
      "route@user@:session", // Empty backend name
  };

  for (const auto& input : invalid_inputs) {
    EXPECT_FALSE(SessionCodec::parseCompositeSessionId(input).ok()) << "Input: " << input;
  }
}

// Backends that don't return mcp-session-id are session-less.
TEST(SessionCodecTest, ParseEmptyBackendSessions) {
  std::string composite = absl::StrCat("route1@", SessionCodec::encode("user1"), "@");

  auto result = SessionCodec::parseCompositeSessionId(composite);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->route, "route1");
  EXPECT_EQ(result->subject, "user1");
  EXPECT_TRUE(result->backend_sessions.empty());
}

// Mixed case: only a subset of backends have sessions. The composite session encodes only those.
TEST(SessionCodecTest, BuildAndParsePartialBackendSessions) {
  absl::flat_hash_map<std::string, std::string> sessions = {{"backend1", "session-abc"}};

  std::string composite = SessionCodec::buildCompositeSessionId("route1", "user1", sessions);

  auto result = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->route, "route1");
  EXPECT_EQ(result->subject, "user1");
  // Only backend1 should be present; backend2 is absent (session-less).
  EXPECT_EQ(result->backend_sessions.size(), 1);
  EXPECT_EQ(result->backend_sessions["backend1"], "session-abc");
  EXPECT_EQ(result->backend_sessions.count("backend2"), 0);
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
