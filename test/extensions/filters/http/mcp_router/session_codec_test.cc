#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;
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

  ASSERT_OK(result);
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

  ASSERT_OK(result);
  EXPECT_EQ(result->route, "my_route");
  EXPECT_EQ(result->subject, subject_with_at);
  EXPECT_THAT(result->backend_sessions, UnorderedElementsAre(Pair("backend1", "session1")));
}

TEST(SessionCodecTest, ParseInvalidCustomFormat) {
  const std::vector<std::string> invalid_inputs = {
      "invalid",
      "no_backends@user",
      "route@user@backend",  // Missing colon
      "route@user@:session", // Empty backend name
  };

  for (const auto& input : invalid_inputs) {
    EXPECT_THAT(SessionCodec::parseCompositeSessionId(input), Not(IsOk())) << "Input: " << input;
  }
}

// Backends that don't return mcp-session-id are session-less.
TEST(SessionCodecTest, ParseEmptyBackendSessions) {
  std::string composite = absl::StrCat("route1@", SessionCodec::encode("user1"), "@");

  auto result = SessionCodec::parseCompositeSessionId(composite);

  ASSERT_OK(result);
  EXPECT_EQ(result->route, "route1");
  EXPECT_EQ(result->subject, "user1");
  EXPECT_TRUE(result->backend_sessions.empty());
}

// Mixed case: only a subset of backends have sessions. The composite session encodes only those.
TEST(SessionCodecTest, BuildAndParsePartialBackendSessions) {
  absl::flat_hash_map<std::string, std::string> sessions = {{"backend1", "session-abc"}};

  std::string composite = SessionCodec::buildCompositeSessionId("route1", "user1", sessions);

  auto result = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_OK(result);
  EXPECT_EQ(result->route, "route1");
  EXPECT_EQ(result->subject, "user1");
  // Only backend1 should be present; backend2 is absent (session-less).
  EXPECT_EQ(result->backend_sessions.size(), 1);
  EXPECT_EQ(result->backend_sessions["backend1"], "session-abc");
  EXPECT_EQ(result->backend_sessions.count("backend2"), 0);
}

TEST(SessionCodecTest, IntegrityRoundTrip) {
  const std::string key = "server-held-secret";
  const std::string composite =
      SessionCodec::buildCompositeSessionId("route1", "user1", {{"backend1", "s1"}});

  const std::string token = SessionCodec::encodeWithIntegrity(composite, key);

  // Wire format is "<base64 payload>.<base64 mac>".
  EXPECT_THAT(token, testing::StartsWith(SessionCodec::encode(composite) + "."));
  EXPECT_EQ(composite, SessionCodec::decodeWithIntegrity(token, key));
}

// An attacker who holds a valid token can read and rewrite the payload (Base64 only), but cannot
// recompute the MAC without the server-held key, so a rebound subject is rejected.
TEST(SessionCodecTest, IntegrityRejectsForgedSubject) {
  const std::string key = "server-held-secret";
  const std::string token = SessionCodec::encodeWithIntegrity(
      SessionCodec::buildCompositeSessionId("route1", "alice", {{"backend1", "s1"}}), key);

  const size_t sep = token.rfind('.');
  ASSERT_NE(sep, std::string::npos);
  const std::string stale_mac = token.substr(sep + 1);
  const std::string forged =
      SessionCodec::encode(SessionCodec::buildCompositeSessionId("route1", "bob",
                                                                 {{"backend1", "s1"}})) +
      "." + stale_mac;

  EXPECT_EQ("", SessionCodec::decodeWithIntegrity(forged, key));
}

TEST(SessionCodecTest, IntegrityRejectsWrongKey) {
  const std::string token =
      SessionCodec::encodeWithIntegrity("route1@dXNlcjE=@backend1:czE=", "server-held-secret");
  EXPECT_EQ("", SessionCodec::decodeWithIntegrity(token, "attacker-guessed-key"));
}

TEST(SessionCodecTest, IntegrityRejectsMalformedTokens) {
  const std::string key = "server-held-secret";
  // No MAC separator, including the legacy unsigned format, is rejected when a key is set.
  EXPECT_EQ("", SessionCodec::decodeWithIntegrity("aGVsbG8=", key));
  // Empty MAC.
  EXPECT_EQ("", SessionCodec::decodeWithIntegrity("aGVsbG8=.", key));
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
