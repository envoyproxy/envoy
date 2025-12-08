#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using testing::_;
using testing::Return;

// Verifies parseMethodString correctly maps all MCP method strings to enum values.
TEST(ParseMethodStringTest, AllMethods) {
  EXPECT_EQ(parseMethodString("initialize"), McpMethod::Initialize);
  EXPECT_EQ(parseMethodString("tools/list"), McpMethod::ToolsList);
  EXPECT_EQ(parseMethodString("tools/call"), McpMethod::ToolsCall);
  EXPECT_EQ(parseMethodString("ping"), McpMethod::Ping);
  EXPECT_EQ(parseMethodString("notifications/initialized"), McpMethod::Notification);
  EXPECT_EQ(parseMethodString("notifications/cancelled"), McpMethod::Notification);
  EXPECT_EQ(parseMethodString("notifications/progress"), McpMethod::Notification);
  EXPECT_EQ(parseMethodString("unknown_method"), McpMethod::Unknown);
  EXPECT_EQ(parseMethodString(""), McpMethod::Unknown);
}

/** Tests for SessionCodec encoding, decoding, and composite session ID handling. */
class SessionCodecTest : public testing::Test {};

// Verifies encode/decode round-trip preserves data.
TEST_F(SessionCodecTest, EncodeDecode) {
  std::string data = "hello world";

  std::string encoded = SessionCodec::encode(data);
  EXPECT_NE(encoded, data);

  std::string decoded = SessionCodec::decode(encoded);
  EXPECT_EQ(decoded, data);
}

// Verifies empty string encoding/decoding.
TEST_F(SessionCodecTest, EncodeDecodeEmpty) {
  std::string encoded = SessionCodec::encode("");
  std::string decoded = SessionCodec::decode(encoded);
  EXPECT_EQ(decoded, "");
}

// Verifies composite session ID contains route, subject, and backend info.
TEST_F(SessionCodecTest, BuildCompositeSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend1", "session-abc"},
      {"backend2", "session-xyz"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route1", "user1", sessions);

  EXPECT_TRUE(absl::StrContains(composite, "route1@"));
  EXPECT_TRUE(absl::StrContains(composite, "@user1@"));
  EXPECT_TRUE(absl::StrContains(composite, "backend1:"));
  EXPECT_TRUE(absl::StrContains(composite, "backend2:"));
}

// Verifies parsing correctly extracts route, subject, and backend sessions.
TEST_F(SessionCodecTest, ParseCompositeSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"time", "sess-time"},
      {"tools", "sess-tools"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("myroute", "myuser", sessions);

  auto parsed = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->route, "myroute");
  EXPECT_EQ(parsed->subject, "myuser");
  EXPECT_EQ(parsed->backend_sessions.size(), 2);
  EXPECT_EQ(parsed->backend_sessions["time"], "sess-time");
  EXPECT_EQ(parsed->backend_sessions["tools"], "sess-tools");
}

// Verifies parsing rejects malformed session IDs.
TEST_F(SessionCodecTest, ParseCompositeSessionIdInvalid) {
  auto result1 = SessionCodec::parseCompositeSessionId("no-at-signs");
  EXPECT_FALSE(result1.ok());

  auto result2 = SessionCodec::parseCompositeSessionId("one@part");
  EXPECT_FALSE(result2.ok());

  auto result3 = SessionCodec::parseCompositeSessionId("route@user@backend-no-colon");
  EXPECT_FALSE(result3.ok());

  auto result4 = SessionCodec::parseCompositeSessionId("route@user@:session");
  EXPECT_FALSE(result4.ok());
}

// Verifies full encode-decode-parse round-trip.
TEST_F(SessionCodecTest, RoundTrip) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend1", "session-123"},
      {"backend2", "session-456"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route", "subject", sessions);
  std::string encoded = SessionCodec::encode(composite);
  std::string decoded = SessionCodec::decode(encoded);

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->route, "route");
  EXPECT_EQ(parsed->subject, "subject");
  EXPECT_EQ(parsed->backend_sessions["backend1"], "session-123");
  EXPECT_EQ(parsed->backend_sessions["backend2"], "session-456");
}

// Verifies special characters in session IDs are handled correctly.
TEST_F(SessionCodecTest, SpecialCharactersInSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend", "sess+with/special=chars"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route", "user", sessions);
  std::string encoded = SessionCodec::encode(composite);
  std::string decoded = SessionCodec::decode(encoded);

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->backend_sessions["backend"], "sess+with/special=chars");
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
