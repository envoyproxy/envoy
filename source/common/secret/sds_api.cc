#include "common/secret/sds_api.h"

#include <unordered_map>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/config/api_version.h"
#include "common/config/resources.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Secret {

SdsApi::SdsApi(envoy::config::core::v3::ConfigSource sds_config, absl::string_view sds_config_name,
               Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
               ProtobufMessage::ValidationVisitor& validation_visitor, Stats::Store& stats,
               Init::Manager& init_manager, std::function<void()> destructor_cb)
    : init_target_(fmt::format("SdsApi {}", sds_config_name), [this] { initialize(); }),
      stats_(stats), sds_config_(std::move(sds_config)), sds_config_name_(sds_config_name),
      secret_hash_(0), clean_up_(std::move(destructor_cb)), validation_visitor_(validation_visitor),
      subscription_factory_(subscription_factory),
      time_source_(time_source), secret_data_{sds_config_name_, "uninitialized",
                                              time_source_.systemTime()} {
  // TODO(JimmyCYJ): Implement chained_init_manager, so that multiple init_manager
  // can be chained together to behave as one init_manager. In that way, we let
  // two listeners which share same SdsApi to register at separate init managers, and
  // each init manager has a chance to initialize its targets.
  init_manager.add(init_target_);
}

void SdsApi::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                            const std::string& version_info) {
  validateUpdateSize(resources.size());
  auto secret =
      MessageUtil::anyConvertAndValidate<envoy::extensions::transport_sockets::tls::v3::Secret>(
          resources[0], validation_visitor_);

  if (secret.name() != sds_config_name_) {
    throw EnvoyException(
        fmt::format("Unexpected SDS secret (expecting {}): {}", sds_config_name_, secret.name()));
  }

  const uint64_t new_hash = MessageUtil::hash(secret);
  if (new_hash != secret_hash_) {
    validateConfig(secret);
    secret_hash_ = new_hash;
    setSecret(secret);
    update_callback_manager_.runCallbacks();
  }
  secret_data_.last_updated_ = time_source_.systemTime();
  secret_data_.version_info_ = version_info;
  init_target_.ready();
}

void SdsApi::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& resources,
    const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
  validateUpdateSize(resources.size());
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> unwrapped_resource;
  *unwrapped_resource.Add() = resources[0].resource();
  onConfigUpdate(unwrapped_resource, resources[0].version());
}

void SdsApi::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                  const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

void SdsApi::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    throw EnvoyException(
        fmt::format("Missing SDS resources for {} in onConfigUpdate()", sds_config_name_));
  }
  if (num_resources != 1) {
    throw EnvoyException(fmt::format("Unexpected SDS secrets length: {}", num_resources));
  }
}

void SdsApi::initialize() {
  const auto resource_name = getResourceName(sds_config_.resource_api_version());
  subscription_ = subscription_factory_.subscriptionFromConfigSource(
      sds_config_, Grpc::Common::typeUrl(resource_name), stats_, *this);
  subscription_->start({sds_config_name_});
}

SdsApi::SecretData SdsApi::secretData() { return secret_data_; }

} // namespace Secret
} // namespace Envoy
