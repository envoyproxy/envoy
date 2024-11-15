#include "envoy/common/exception.h"
#include "envoy/network/socket.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/default_local_address_selector_factory.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(ConfigTest, EmptyUpstreamAddresses) {
  DefaultUpstreamLocalAddressSelectorFactory factory;
  std::vector<UpstreamLocalAddress> upstream_local_addresses;
  EXPECT_EQ(factory.createLocalAddressSelector(upstream_local_addresses, absl::nullopt)
                .status()
                .message(),
            "Bootstrap's upstream binding config has no valid source address.");
}

TEST(ConfigTest, NullUpstreamAddress) {
  DefaultUpstreamLocalAddressSelectorFactory factory;
  std::vector<UpstreamLocalAddress> upstream_local_addresses;
  upstream_local_addresses.emplace_back(
      UpstreamLocalAddress{nullptr, std::make_shared<Network::ConnectionSocket::Options>()});
  // This should be exception free.
  UpstreamLocalAddressSelectorConstSharedPtr selector =
      factory.createLocalAddressSelector(upstream_local_addresses, absl::nullopt).value();
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

} // namespace
} // namespace Upstream
} // namespace Envoy
