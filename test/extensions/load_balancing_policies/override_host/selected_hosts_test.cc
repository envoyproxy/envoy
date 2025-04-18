#include <utility>

#include "source/extensions/load_balancing_policies/override_host/selected_hosts.h"

#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {
namespace {

TEST(SelectedHostsTest, ValidIPv4) {
  auto selected_hosts_result = SelectedHosts::make("1.2.3.4:1234");
  EXPECT_TRUE(selected_hosts_result.ok());
  auto selected_hosts = std::move(selected_hosts_result.value());
  EXPECT_EQ(selected_hosts->primary.address.address, "1.2.3.4");
  EXPECT_EQ(selected_hosts->primary.address.port, 1234);
  EXPECT_TRUE(selected_hosts->failover.empty());
}

TEST(SelectedHostsTest, ValidIPv6) {
  auto selected_hosts_result = SelectedHosts::make("[1:2:3::4]:1234");
  EXPECT_TRUE(selected_hosts_result.ok());
  auto selected_hosts = std::move(selected_hosts_result.value());
  EXPECT_EQ(selected_hosts->primary.address.address, "1:2:3::4");
  EXPECT_EQ(selected_hosts->primary.address.port, 1234);
  EXPECT_TRUE(selected_hosts->failover.empty());
}

TEST(SelectedHostsTest, MultipleEndpoints) {
  auto selected_hosts_result = SelectedHosts::make("1.2.3.4:1234, 5.6.7.8:5678");
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Primary endpoint is not a single address"));
}

TEST(SelectedHostsTest, EmptyAddress) {
  auto selected_hosts_result = SelectedHosts::make("");
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Primary endpoint is not a single address"));
}

TEST(SelectedHostsTest, BadAddress) {
  auto selected_hosts_result = SelectedHosts::make("i'm not an address");
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       "Address 'i'm not an address' is not in host:port format"));
}

TEST(SelectedHostsTest, ProtoInvalidMultipleEndpoints) {
  auto selected_hosts_result = SelectedHosts::make("1.2.3.4:1234, , 5.6.7.8:5678");
  EXPECT_THAT(selected_hosts_result,
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument, "Address is empty"));
}

} // namespace
} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
