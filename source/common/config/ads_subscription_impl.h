#pragma once

#include "envoy/config/ads.h"
#include "envoy/config/subscription.h"

#include "common/common/logger.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "api/discovery.pb.h"

namespace Envoy {
namespace Config {

/**
 * Adapter from typed Subscription to untyped AdsApi. Also handles per-xDS API stats/logging.
 */
template <class ResourceType>
class AdsSubscriptionImpl : public Subscription<ResourceType>,
                            AdsCallbacks,
                            Logger::Loggable<Logger::Id::config> {
public:
  AdsSubscriptionImpl(AdsApi& ads_api, SubscriptionStats stats)
      : ads_api_(ads_api), stats_(stats),
        type_url_(Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             SubscriptionCallbacks<ResourceType>& callbacks) override {
    callbacks_ = &callbacks;
    watch_ = ads_api_.subscribe(type_url_, resources, *this);
    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // gRPC/filesystem/REST Subscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    watch_ = ads_api_.subscribe(type_url_, resources, *this);
  }

  // Config::AdsCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources) override {
    Protobuf::RepeatedPtrField<ResourceType> typed_resources;
    std::transform(resources.cbegin(), resources.cend(),
                   Protobuf::RepeatedPtrFieldBackInserter(&typed_resources),
                   MessageUtil::anyConvert<ResourceType>);
    callbacks_->onConfigUpdate(typed_resources);
    stats_.update_success_.inc();
    stats_.update_attempt_.inc();
    ENVOY_LOG(debug, "ADS config for {} accepted with {} resources", type_url_, resources.size());
    for (const auto resource : typed_resources) {
      ENVOY_LOG(debug, "- {}", resource.DebugString());
    }
  }

  void onConfigUpdateFailed(const EnvoyException* e) override {
    stats_.update_rejected_.inc();
    stats_.update_attempt_.inc();
    ENVOY_LOG(warn, "ADS config for {} rejected: {}", type_url_, e == nullptr ? "" : e->what());
    callbacks_->onConfigUpdateFailed(e);
  }

private:
  AdsApi& ads_api_;
  SubscriptionStats stats_;
  const std::string type_url_;
  SubscriptionCallbacks<ResourceType>* callbacks_{};
  AdsWatchPtr watch_{};
};

} // namespace Config
} // namespace Envoy
