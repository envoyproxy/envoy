#pragma once

#include "common/config/ads_subscription_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAreArray;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Config {

typedef AdsSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> AdsEdsSubscriptionImpl;

class AdsSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  AdsSubscriptionTestHarness() {
    subscription_.reset(new AdsEdsSubscriptionImpl(ads_api_, stats_));
  }

  ~AdsSubscriptionTestHarness() { EXPECT_CALL(*ads_watch_, cancel()); }

  AdsWatch* resetAdsWatch() {
    ads_watch_ = new Config::MockAdsWatch();
    return ads_watch_;
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    UNREFERENCED_PARAMETER(cluster_names);
    UNREFERENCED_PARAMETER(version);
  }

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    EXPECT_CALL(ads_api_, subscribe_("type.googleapis.com/envoy.api.v2.ClusterLoadAssignment",
                                     ElementsAreArray(cluster_names), _))
        .WillOnce(Return(resetAdsWatch()));
    subscription_->start(cluster_names, callbacks_);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) override {
    Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> typed_resources;
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
    for (const auto& cluster : cluster_names) {
      envoy::api::v2::ClusterLoadAssignment* load_assignment = typed_resources.Add();
      load_assignment->set_cluster_name(cluster);
      resources.Add()->PackFrom(*load_assignment);
    }

    EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(typed_resources)))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      subscription_->onConfigUpdate(resources);
    } else {
      EXPECT_THROW_WITH_MESSAGE(subscription_->onConfigUpdate(resources), EnvoyException,
                                "bad config");
      EnvoyException bad_config("bad config");
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(&bad_config));
      subscription_->onConfigUpdateFailed(&bad_config);
    }

    UNREFERENCED_PARAMETER(version);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    EXPECT_CALL(*ads_watch_, cancel());
    EXPECT_CALL(ads_api_, subscribe_("type.googleapis.com/envoy.api.v2.ClusterLoadAssignment",
                                     ElementsAreArray(cluster_names), _))
        .WillOnce(Return(resetAdsWatch()));
    subscription_->updateResources(cluster_names);
  }

  Config::MockAdsApi ads_api_;
  Config::MockAdsWatch* ads_watch_;
  std::unique_ptr<AdsEdsSubscriptionImpl> subscription_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
};

} // namespace Config
} // namespace Envoy
