#include "envoy/registry/registry.h"

#include "source/extensions/common/dynamic_forward_proxy/dynamic_host_candidates.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

TEST(DynamicHostCandidatesTest, ParsesHostsAndPorts) {
  auto candidates =
      DynamicHostCandidates::fromString("a.example.com, b.example.com:8443,[::1]:9000,127.0.0.1");
  ASSERT_NE(nullptr, candidates);
  ASSERT_EQ(4, candidates->candidates().size());
  EXPECT_EQ("a.example.com", candidates->candidates()[0].host);
  EXPECT_EQ(0, candidates->candidates()[0].port);
  EXPECT_EQ("b.example.com", candidates->candidates()[1].host);
  EXPECT_EQ(8443, candidates->candidates()[1].port);
  EXPECT_EQ("[::1]", candidates->candidates()[2].host);
  EXPECT_EQ(9000, candidates->candidates()[2].port);
  EXPECT_EQ("127.0.0.1", candidates->candidates()[3].host);
  EXPECT_EQ("a.example.com,b.example.com:8443,[::1]:9000,127.0.0.1",
            candidates->serializeAsString().value());
}

TEST(DynamicHostCandidatesTest, RejectsMalformedLists) {
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString(""));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("a.example.com,,b.example.com"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("a.example.com:0"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString(":443"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("a.example.com:abc"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("2001:db8::1"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("fe80::abcd"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("::1"));
  EXPECT_EQ(nullptr, DynamicHostCandidates::fromString("[::1"));
}

TEST(DynamicHostCandidatesTest, ObjectFactory) {
  auto* factory = Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
      DynamicHostCandidates::key());
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(DynamicHostCandidates::key(), factory->name());

  auto object = factory->createFromBytes("a.example.com,b.example.com:8443");
  ASSERT_NE(nullptr, object);
  EXPECT_EQ("a.example.com,b.example.com:8443", object->serializeAsString().value());
  EXPECT_EQ(nullptr, factory->createFromBytes("a.example.com:"));
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
