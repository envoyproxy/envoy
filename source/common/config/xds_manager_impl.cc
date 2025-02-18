#include "source/common/config/xds_manager_impl.h"

#include "envoy/config/core/v3/config_source.pb.validate.h"

#include "source/common/common/thread.h"

namespace Envoy {
namespace Config {

absl::Status XdsManagerImpl::initialize(Upstream::ClusterManager* cm) {
  ASSERT(cm != nullptr);
  cm_ = cm;
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
