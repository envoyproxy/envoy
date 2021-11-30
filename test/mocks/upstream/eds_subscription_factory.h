#pragma once

#include "source/common/upstream/eds_subscription_factory.h"

namespace Envoy {
namespace Upstream {
class MockEdsSubscriptionFactory : public EdsSubscriptionFactory {
  MOCK_METHOD(Config::GrpcMuxSharedPtr, getOrCreateMux,
              (Grpc::RawAsyncClientPtr, const Protobuf::MethodDescriptor&, Random::RandomGenerator&,
               const envoy::config::core::v3::ApiConfigSource&, Stats::Scope&,
               const Config::RateLimitSettings&));

  MOCK_METHOD(Config::SubscriptionPtr, subscriptionFromConfigSource,
              (const envoy::config::core::v3::ConfigSource&, absl::string_view, Stats::Scope&,
               Config::SubscriptionCallbacks&, Config::OpaqueResourceDecoder&,
               const Config::SubscriptionOptions&));

  MOCK_METHOD(Config::SubscriptionPtr, collectionSubscriptionFromUrl,
              (const xds::core::v3::ResourceLocator&, const envoy::config::core::v3::ConfigSource&,
               absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
               Config::OpaqueResourceDecoder&));
};

} // namespace Upstream

} // namespace Envoy
