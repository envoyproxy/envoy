#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/local_address_selectors/filter_state_override/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace FilterStateOverride {
namespace {

TEST(ConfigTest, EmptyUpstreamAddresses) {
  NamespaceLocalAddressSelectorFactory factory;
  std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses;
  EXPECT_EQ(factory.createLocalAddressSelector(upstream_local_addresses, absl::nullopt)
                .status()
                .message(),
            "Bootstrap's upstream binding config has no valid source address.");
}

TEST(ConfigTest, NullUpstreamAddress) {
  NamespaceLocalAddressSelectorFactory factory;
  std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses;
  upstream_local_addresses.emplace_back(Upstream::UpstreamLocalAddress{nullptr, nullptr});
  const auto endpoint = std::make_shared<const Network::Address::Ipv4Instance>("10.10.10.10");
  const auto selector = factory.createLocalAddressSelector(upstream_local_addresses, absl::nullopt);
  ASSERT_TRUE(selector.ok());
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint, nullptr, {});
  EXPECT_EQ(nullptr, result.address_);
}

constexpr absl::string_view BadValue = "I'm bad";

class TestObject : public StreamInfo::FilterState::Object {
public:
  TestObject(absl::string_view value) : value_(value) {}
  absl::optional<std::string> serializeAsString() const override {
    if (value_ == BadValue) {
      return {};
    }
    return std::string(value_);
  }

private:
  const std::string value_;
};

constexpr absl::string_view FilterStateKey =
    "envoy.network.upstream_bind_override.network_namespace";

Network::TransportSocketOptionsConstSharedPtr optionsWithOverride(absl::string_view netns) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData(FilterStateKey, std::make_shared<TestObject>(netns),
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection,
                       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce);
  return Network::TransportSocketOptionsUtility::fromFilterState(filter_state);
}

template <class... Args>
void validateNamespaceOverride(absl::optional<std::string> netns, bool ipv6, Args&&... args) {
  Network::Address::InstanceConstSharedPtr upstream_address;
  if (ipv6) {
    upstream_address =
        std::make_shared<const Network::Address::Ipv6Instance>(std::forward<Args>(args)...);
  } else {
    upstream_address =
        std::make_shared<const Network::Address::Ipv4Instance>(std::forward<Args>(args)...);
  }
  NamespaceLocalAddressSelectorFactory factory;
  std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses;
  upstream_local_addresses.emplace_back(Upstream::UpstreamLocalAddress{upstream_address, nullptr});
  const auto endpoint = std::make_shared<const Network::Address::Ipv4Instance>("10.10.10.10");
  Network::TransportSocketOptionsConstSharedPtr options =
      netns ? optionsWithOverride(*netns) : nullptr;
  const auto selector = factory.createLocalAddressSelector(upstream_local_addresses, absl::nullopt);
  ASSERT_TRUE(selector.ok());
  const auto result = selector.value()->getUpstreamLocalAddress(endpoint, nullptr,
                                                                makeOptRefFromPtr(options.get()));
  EXPECT_NE(nullptr, result.address_);
  EXPECT_EQ(result.address_->asStringView(), upstream_address->asStringView());
  if (netns) {
    if (netns->empty()) {
      // Override with empty string clear the namespace.
      EXPECT_EQ(absl::nullopt, result.address_->networkNamespace());
    } else if (*netns == BadValue) {
      // Override with a bad filter state object is a no-op.
      EXPECT_EQ(upstream_address->networkNamespace(), result.address_->networkNamespace());
    } else {
      // Override with any other value sets that value.
      EXPECT_EQ(*netns, result.address_->networkNamespace());
    }
  } else {
    // No override means the bind address namespace is preserved.
    EXPECT_EQ(upstream_address->networkNamespace(), result.address_->networkNamespace());
  }
}

TEST(ConfigTest, NamespaceOverrideEffective) {
  {
    SCOPED_TRACE("IPv4 override present");
    validateNamespaceOverride("/var/run/netns/1", false, "1.2.3.4", 8000);
  }
  {
    SCOPED_TRACE("IPv4 override present with existing namespace");
    validateNamespaceOverride("/var/run/netns/1", false, "1.2.3.4", 8000, nullptr,
                              "/var/run/netns/2");
  }
  {
    SCOPED_TRACE("IPv4 override absent");
    validateNamespaceOverride({}, false, "1.2.3.4", 8000);
  }
  {
    SCOPED_TRACE("IPv4 override absent with existing namespace");
    validateNamespaceOverride({}, false, "1.2.3.4", 8000, nullptr, "/var/run/netns/2");
  }
  {
    SCOPED_TRACE("IPv4 empty override present with existing namespace");
    validateNamespaceOverride("", false, "1.2.3.4", 8000, nullptr, "/var/run/netns/2");
  }
  {
    SCOPED_TRACE("IPv4 try to override with a bad filter state");
    validateNamespaceOverride(std::string(BadValue), false, "1.2.3.4", 8000, nullptr,
                              "/var/run/netns/2");
  }
  {
    SCOPED_TRACE("IPv6 override present");
    validateNamespaceOverride("/var/run/netns/1", true, "::0001", 8000);
  }
  {
    SCOPED_TRACE("IPv6 override present with existing namespace");
    validateNamespaceOverride("/var/run/netns/1", true, "::0001", nullptr, "/var/run/netns/3");
  }
  {
    SCOPED_TRACE("IPv6 override absent");
    validateNamespaceOverride({}, true, "::0001", 8000);
  }
  {
    SCOPED_TRACE("IPv6 override absent with existing namespace");
    validateNamespaceOverride({}, true, "::0001", nullptr, "/var/run/netns/3");
  }
  {
    SCOPED_TRACE("IPv6 empty override present with existing namespace");
    validateNamespaceOverride("", true, "::0001", nullptr, "/var/run/netns/3");
  }
}

} // namespace
} // namespace FilterStateOverride
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
