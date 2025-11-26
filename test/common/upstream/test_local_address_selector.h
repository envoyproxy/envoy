#pragma once

#include <string>
#include <vector>

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

class TestUpstreamLocalAddressSelector : public UpstreamLocalAddressSelector {
public:
  TestUpstreamLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
      std::shared_ptr<size_t> num_calls, bool return_empty_source_address = false)
      : upstream_local_addresses_{std::move(upstream_local_addresses)}, num_calls_{num_calls},
        return_empty_source_address_{return_empty_source_address} {}

  UpstreamLocalAddress
  getUpstreamLocalAddressImpl(const Network::Address::InstanceConstSharedPtr&,
                              OptRef<const Network::TransportSocketOptions>) const override {
    ++(*num_calls_);
    if (return_empty_source_address_) {
      return {};
    }
    current_idx_ = (current_idx_ + 1) % upstream_local_addresses_.size();
    return upstream_local_addresses_[current_idx_];
  }

private:
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
  mutable size_t current_idx_ = 0;
  const std::shared_ptr<size_t> num_calls_;
  const bool return_empty_source_address_ = false;
};

class TestUpstreamLocalAddressSelectorFactory : public UpstreamLocalAddressSelectorFactory {
public:
  TestUpstreamLocalAddressSelectorFactory(
      std::shared_ptr<size_t> num_calls = std::make_shared<size_t>(0),
      bool return_empty_source_address = false)
      : num_calls_(num_calls), return_empty_source_address_{return_empty_source_address} {}

  absl::StatusOr<UpstreamLocalAddressSelectorConstSharedPtr> createLocalAddressSelector(
      std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
      absl::optional<std::string>) const override {
    return std::make_shared<TestUpstreamLocalAddressSelector>(upstream_local_addresses, num_calls_,
                                                              return_empty_source_address_);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Empty>();
  }

  std::string name() const override { return "test.upstream.local.address.selector"; }

private:
  std::shared_ptr<size_t> num_calls_;
  const bool return_empty_source_address_ = false;
};

} // namespace Upstream
} // namespace Envoy
