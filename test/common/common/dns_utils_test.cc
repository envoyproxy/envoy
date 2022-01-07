#include "source/common/common/dns_utils.h"
#include "source/common/network/utility.h"

#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(DnsUtils, LegacyGenerateTest) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.allow_multiple_dns_addresses", "false"}});

  std::list<Network::DnsResponse> responses =
      TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.2"});
  std::vector<Network::Address::InstanceConstSharedPtr> addresses =
      DnsUtils::generateAddressList(responses, 2);
  EXPECT_EQ(0, addresses.size());
}

TEST(DnsUtils, MultipleGenerateTest) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.allow_multiple_dns_addresses", "true"}});

  std::list<Network::DnsResponse> responses =
      TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.2"});
  std::vector<Network::Address::InstanceConstSharedPtr> addresses =
      DnsUtils::generateAddressList(responses, 2);
  ASSERT_EQ(2, addresses.size());
  EXPECT_EQ(addresses[0]->asString(), "10.0.0.1:2");
  EXPECT_EQ(addresses[1]->asString(), "10.0.0.2:2");
}

TEST(DnsUtils, ListChanged) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.allow_multiple_dns_addresses", "true"}});

  Network::Address::InstanceConstSharedPtr address1 =
      Network::Utility::parseInternetAddress("10.0.0.1");
  Network::Address::InstanceConstSharedPtr address1_dup =
      Network::Utility::parseInternetAddress("10.0.0.1");
  Network::Address::InstanceConstSharedPtr address2 =
      Network::Utility::parseInternetAddress("10.0.0.2");

  std::vector<Network::Address::InstanceConstSharedPtr> addresses1 = {address1, address2};
  std::vector<Network::Address::InstanceConstSharedPtr> addresses2 = {address1_dup, address2};
  EXPECT_FALSE(DnsUtils::listChanged(addresses1, addresses2));

  std::vector<Network::Address::InstanceConstSharedPtr> addresses3 = {address2, address1};
  EXPECT_TRUE(DnsUtils::listChanged(addresses1, addresses3));
  EXPECT_TRUE(DnsUtils::listChanged(addresses1, {address2}));
}

} // namespace
} // namespace Envoy
