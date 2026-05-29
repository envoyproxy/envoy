#pragma once

#include <memory>
#include <string>

#include "source/common/common/statusor.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/cert_validator/factory.h"
#include "source/common/tls/stats.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {

// Function pointer types for cert validator ABI functions.
using OnCertValidatorConfigNewType = decltype(&envoy_dynamic_module_on_cert_validator_config_new);
using OnCertValidatorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_cert_validator_config_destroy);
using OnCertValidatorDoVerifyCertChainType =
    decltype(&envoy_dynamic_module_on_cert_validator_do_verify_cert_chain);
using OnCertValidatorGetSslVerifyModeType =
    decltype(&envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode);
using OnCertValidatorUpdateDigestType =
    decltype(&envoy_dynamic_module_on_cert_validator_update_digest);

/**
 * Configuration holding the resolved dynamic module and ABI function pointers
 * for cert validation. This is shared between the factory and the validator.
 */
class DynamicModuleCertValidatorConfig {
public:
  DynamicModuleCertValidatorConfig(
      const std::string& validator_name, const std::string& validator_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleCertValidatorConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_cert_validator_config_module_ptr in_module_config_ = nullptr;

  // Resolved function pointers. All are guaranteed non-null after successful creation.
  OnCertValidatorConfigDestroyType on_config_destroy_ = nullptr;
  OnCertValidatorDoVerifyCertChainType on_do_verify_cert_chain_ = nullptr;
  OnCertValidatorGetSslVerifyModeType on_get_ssl_verify_mode_ = nullptr;
  OnCertValidatorUpdateDigestType on_update_digest_ = nullptr;

  const std::string& validatorName() const { return validator_name_; }
  const std::string& validatorConfig() const { return validator_config_; }

  // Stores error details set by the module via the set_error_details callback during
  // do_verify_cert_chain. Reset before each verification call.
  absl::optional<std::string> last_error_details_;

  // Stores the transport socket callbacks pointer during do_verify_cert_chain so that
  // filter state callbacks can access the connection's stream info. Reset after each call.
  Network::TransportSocketCallbacks* current_callbacks_ = nullptr;

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleCertValidatorConfig>>
  newDynamicModuleCertValidatorConfig(
      const std::string& validator_name, const std::string& validator_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string validator_name_;
  const std::string validator_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleCertValidatorConfigSharedPtr = std::shared_ptr<DynamicModuleCertValidatorConfig>;

/**
 * Creates a new DynamicModuleCertValidatorConfig. Resolves all ABI symbols and creates the
 * in-module config. Returns an error if symbol resolution or module initialization fails.
 */
absl::StatusOr<DynamicModuleCertValidatorConfigSharedPtr> newDynamicModuleCertValidatorConfig(
    const std::string& validator_name, const std::string& validator_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * CertValidator implementation backed by a dynamic module.
 */
class DynamicModuleCertValidator : public CertValidator,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleCertValidator(DynamicModuleCertValidatorConfigSharedPtr config, SslStats& stats);

  ~DynamicModuleCertValidator() override = default;

  // CertValidator interface.
  absl::Status addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;

  absl::StatusOr<int> initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                            bool handshaker_provides_certificates,
                                            Stats::Scope& scope) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  absl::optional<uint32_t> daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override;
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

private:
  DynamicModuleCertValidatorConfigSharedPtr config_;
  SslStats& stats_;
};

/**
 * Factory for creating DynamicModuleCertValidator instances.
 */
class DynamicModuleCertValidatorFactory : public CertValidatorFactory {
public:
  absl::StatusOr<CertValidatorPtr>
  createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                      Server::Configuration::CommonFactoryContext& context,
                      Stats::Scope& scope) override;

  std::string name() const override { return "envoy.tls.cert_validator.dynamic_modules"; }
};

DECLARE_FACTORY(DynamicModuleCertValidatorFactory);

} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
