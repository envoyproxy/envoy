#include "library/common/common/system_helper.h"

namespace Envoy {

namespace {
class DefaultSystemHelper : public SystemHelper {
  // SystemHelper:
  bool isCleartextPermitted(absl::string_view /*hostname*/) override {
    return false;
  }

  envoy_cert_validation_result validateCertificateChain(const envoy_data* /*certs*/, uint8_t /*size*/,
                                                        const char* /*host_name*/) override {
    envoy_cert_validation_result result;
    result.result = ENVOY_SUCCESS;
    result.tls_alert = 0;
    result.error_details = "";
    return result;
  }

  void cleanupAfterCertificateValidation() override {
  }

};

}  // namespace
std::unique_ptr<SystemHelper> SystemHelper::instance_ = std::make_unique<DefaultSystemHelper>();;

SystemHelper& SystemHelper::getInstance() {
  return *instance_;
}

}  // namespace Envoy
