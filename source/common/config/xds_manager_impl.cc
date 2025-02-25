#include "source/common/config/xds_manager_impl.h"

#include "envoy/config/core/v3/config_source.pb.validate.h"

#include "source/common/common/thread.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Config {

absl::Status XdsManagerImpl::initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                        Upstream::ClusterManager* cm) {
  ASSERT(cm != nullptr);
  cm_ = cm;

  // Initialize the XdsResourceDelegate extension, if set on the bootstrap config.
  if (bootstrap.has_xds_delegate_extension()) {
    auto& factory = Config::Utility::getAndCheckFactory<XdsResourcesDelegateFactory>(
        bootstrap.xds_delegate_extension());
    xds_resources_delegate_ = factory.createXdsResourcesDelegate(
        bootstrap.xds_delegate_extension().typed_config(),
        validation_context_.dynamicValidationVisitor(), api_, main_thread_dispatcher_);
  }

  // Initialize the XdsConfigTracker extension, if set on the bootstrap config.
  if (bootstrap.has_xds_config_tracker_extension()) {
    auto& tracker_factory = Config::Utility::getAndCheckFactory<Config::XdsConfigTrackerFactory>(
        bootstrap.xds_config_tracker_extension());
    xds_config_tracker_ = tracker_factory.createXdsConfigTracker(
        bootstrap.xds_config_tracker_extension().typed_config(),
        validation_context_.dynamicValidationVisitor(), api_, main_thread_dispatcher_);
  }

  return absl::OkStatus();
}

absl::Status
XdsManagerImpl::setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  RETURN_IF_NOT_OK(validateAdsConfig(config_source));

  return cm_->replaceAdsMux(config_source);
}

absl::Status
XdsManagerImpl::validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source) {
  auto& validation_visitor = validation_context_.staticValidationVisitor();
  TRY_ASSERT_MAIN_THREAD { MessageUtil::validate(config_source, validation_visitor); }
  END_TRY
  CATCH(const EnvoyException& e, { return absl::InternalError(e.what()); });
  return absl::OkStatus();
}

} // namespace Config
} // namespace Envoy
