#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

/**
 * Adapter from typed Subscription to untyped GrpcMux. Also handles per-xDS API stats/logging.
 */
template <class ResourceType>
class GrpcMuxSubscriptionImpl : public Subscription<ResourceType>,
                                GrpcMuxCallbacks,
                                Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxSubscriptionImpl(GrpcMux& grpc_mux, SubscriptionStats stats)
      : grpc_mux_(grpc_mux), stats_(stats),
        type_url_(Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             SubscriptionCallbacks<ResourceType>& callbacks) override {
    callbacks_ = &callbacks;
    watch_ = grpc_mux_.subscribe(type_url_, resources, *this);
    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // gRPC/filesystem/REST Subscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    watch_ = grpc_mux_.subscribe(type_url_, resources, *this);
    stats_.update_attempt_.inc();
  }

  // Config::GrpcMuxCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override {
    Protobuf::RepeatedPtrField<ResourceType> typed_resources;
    std::transform(resources.cbegin(), resources.cend(),
                   Protobuf::RepeatedPtrFieldBackInserter(&typed_resources),
                   MessageUtil::anyConvert<ResourceType>);
    // TODO(mattklein123): In the future if we start tracking per-resource versions, we need to
    // supply those versions to onConfigUpdate() along with the xDS response ("system")
    // version_info. This way, both types of versions can be tracked and exposed for debugging by
    // the configuration update targets.
    callbacks_->onConfigUpdate(typed_resources, version_info);
    stats_.update_success_.inc();
    stats_.update_attempt_.inc();
    stats_.version_.set(HashUtil::xxHash64(version_info));
    ENVOY_LOG(debug, "gRPC config for {} accepted with {} resources: {}", type_url_,
              resources.size(), RepeatedPtrUtil::debugString(typed_resources));
  }

  void onConfigUpdateFailed(const EnvoyException* e) override {
    // TODO(htuch): Less fragile signal that this is failure vs. reject.
    if (e == nullptr) {
      stats_.update_failure_.inc();
      ENVOY_LOG(warn, "gRPC update for {} failed", type_url_);
    } else {
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "gRPC config for {} rejected: {}", type_url_, e->what());
    }
    stats_.update_attempt_.inc();
    callbacks_->onConfigUpdateFailed(e);
  }

  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return callbacks_->resourceName(resource);
  }

private:
  GrpcMux& grpc_mux_;
  SubscriptionStats stats_;
  const std::string type_url_;
  SubscriptionCallbacks<ResourceType>* callbacks_{};
  GrpcMuxWatchPtr watch_{};
};

} // namespace Config
} // namespace Envoy
