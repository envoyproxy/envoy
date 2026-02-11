#include "source/extensions/transport_sockets/tls/cert_validator/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/cert_validator/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_validator/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {

DynamicModuleCertValidatorConfig::DynamicModuleCertValidatorConfig(
    const std::string& validator_name, const std::string& validator_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : validator_name_(validator_name), validator_config_(validator_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleCertValidatorConfig::~DynamicModuleCertValidatorConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleCertValidatorConfigSharedPtr> newDynamicModuleCertValidatorConfig(
    const std::string& validator_name, const std::string& validator_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {

  auto on_config_new = dynamic_module->getFunctionPointer<OnCertValidatorConfigNewType>(
      "envoy_dynamic_module_on_cert_validator_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnCertValidatorConfigDestroyType>(
      "envoy_dynamic_module_on_cert_validator_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_do_verify = dynamic_module->getFunctionPointer<OnCertValidatorDoVerifyCertChainType>(
      "envoy_dynamic_module_on_cert_validator_do_verify_cert_chain");
  RETURN_IF_NOT_OK_REF(on_do_verify.status());

  auto on_get_verify_mode = dynamic_module->getFunctionPointer<OnCertValidatorGetSslVerifyModeType>(
      "envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode");
  RETURN_IF_NOT_OK_REF(on_get_verify_mode.status());

  auto on_update_digest = dynamic_module->getFunctionPointer<OnCertValidatorUpdateDigestType>(
      "envoy_dynamic_module_on_cert_validator_update_digest");
  RETURN_IF_NOT_OK_REF(on_update_digest.status());

  auto config = std::make_shared<DynamicModuleCertValidatorConfig>(validator_name, validator_config,
                                                                   std::move(dynamic_module));

  config->on_config_destroy_ = on_config_destroy.value();
  config->on_do_verify_cert_chain_ = on_do_verify.value();
  config->on_get_ssl_verify_mode_ = on_get_verify_mode.value();
  config->on_update_digest_ = on_update_digest.value();

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {validator_name.data(),
                                                        validator_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {validator_config.data(),
                                                          validator_config.size()};
  config->in_module_config_ =
      on_config_new.value()(static_cast<void*>(config.get()), name_buffer, config_buffer);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module cert validator config");
  }
  return config;
}

// DynamicModuleCertValidator implementation.

DynamicModuleCertValidator::DynamicModuleCertValidator(
    DynamicModuleCertValidatorConfigSharedPtr config, SslStats& stats)
    : config_(std::move(config)), stats_(stats) {}

absl::Status DynamicModuleCertValidator::addClientValidationContext(SSL_CTX* context,
                                                                    bool require_client_cert) {
  // Set the verify mode on the SSL context based on the module's configuration.
  if (require_client_cert) {
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  } else {
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
  }
  return absl::OkStatus();
}

ValidationResults DynamicModuleCertValidator::doVerifyCertChain(
    STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr /*callback*/,
    const Network::TransportSocketOptionsConstSharedPtr& /*transport_socket_options*/,
    SSL_CTX& /*ssl_ctx*/, const CertValidator::ExtraValidationContext& /*validation_context*/,
    bool is_server, absl::string_view host_name) {

  const int num_certs = sk_X509_num(&cert_chain);
  if (num_certs == 0) {
    stats_.fail_verify_error_.inc();
    const char* error = "verify cert failed: empty cert chain";
    ENVOY_LOG(debug, error);
    return {ValidationResults::ValidationStatus::Failed,
            Envoy::Ssl::ClientValidationStatus::NoClientCertificate, absl::nullopt, error};
  }

  // Encode certificates to DER.
  std::vector<std::vector<uint8_t>> der_certs(num_certs);
  std::vector<envoy_dynamic_module_type_envoy_buffer> cert_buffers(num_certs);
  for (int i = 0; i < num_certs; i++) {
    X509* cert = sk_X509_value(&cert_chain, i);
    uint8_t* der = nullptr;
    int der_len = i2d_X509(cert, &der);
    if (der_len <= 0 || der == nullptr) {
      stats_.fail_verify_error_.inc();
      const char* error = "verify cert failed: DER encoding error";
      ENVOY_LOG(debug, error);
      return {ValidationResults::ValidationStatus::Failed,
              Envoy::Ssl::ClientValidationStatus::Failed, absl::nullopt, error};
    }
    der_certs[i].assign(der, der + der_len);
    OPENSSL_free(der);
    cert_buffers[i] = {reinterpret_cast<const char*>(der_certs[i].data()), der_certs[i].size()};
  }

  envoy_dynamic_module_type_envoy_buffer host_name_buffer = {host_name.data(), host_name.size()};

  // Call the module's verify function.
  auto result = config_->on_do_verify_cert_chain_(
      static_cast<void*>(config_.get()), config_->in_module_config_, cert_buffers.data(),
      static_cast<size_t>(num_certs), host_name_buffer, is_server);

  // Translate the result.
  ValidationResults::ValidationStatus status;
  if (result.status == envoy_dynamic_module_type_cert_validator_validation_status_Successful) {
    status = ValidationResults::ValidationStatus::Successful;
  } else {
    status = ValidationResults::ValidationStatus::Failed;
    stats_.fail_verify_error_.inc();
  }

  Envoy::Ssl::ClientValidationStatus detailed_status;
  switch (result.detailed_status) {
  case envoy_dynamic_module_type_cert_validator_client_validation_status_NotValidated:
    detailed_status = Envoy::Ssl::ClientValidationStatus::NotValidated;
    break;
  case envoy_dynamic_module_type_cert_validator_client_validation_status_Validated:
    detailed_status = Envoy::Ssl::ClientValidationStatus::Validated;
    break;
  case envoy_dynamic_module_type_cert_validator_client_validation_status_NoClientCertificate:
    detailed_status = Envoy::Ssl::ClientValidationStatus::NoClientCertificate;
    break;
  case envoy_dynamic_module_type_cert_validator_client_validation_status_Failed:
    detailed_status = Envoy::Ssl::ClientValidationStatus::Failed;
    break;
  default:
    detailed_status = Envoy::Ssl::ClientValidationStatus::NotValidated;
    break;
  }

  absl::optional<uint8_t> tls_alert;
  if (result.has_tls_alert) {
    tls_alert = result.tls_alert;
  }

  absl::optional<std::string> error_details;
  if (result.error_details.length > 0 && result.error_details.ptr != nullptr) {
    error_details = std::string(result.error_details.ptr, result.error_details.length);
    ENVOY_LOG(debug, "verify cert failed: {}", error_details.value());
  }

  return {status, detailed_status, tls_alert, error_details};
}

absl::StatusOr<int>
DynamicModuleCertValidator::initializeSslContexts(std::vector<SSL_CTX*> /*contexts*/,
                                                  bool handshaker_provides_certificates,
                                                  Stats::Scope& /*scope*/) {
  return config_->on_get_ssl_verify_mode_(config_->in_module_config_,
                                          handshaker_provides_certificates);
}

void DynamicModuleCertValidator::updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                                          uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                                          unsigned hash_length) {
  envoy_dynamic_module_type_module_buffer digest_data = {nullptr, 0};
  config_->on_update_digest_(config_->in_module_config_, &digest_data);

  if (digest_data.ptr != nullptr && digest_data.length > 0) {
    EVP_DigestUpdate(md.get(), digest_data.ptr, digest_data.length);
  }

  // Also hash the validator name and config to ensure uniqueness.
  const auto& name = config_->validatorName();
  const auto& config = config_->validatorConfig();
  EVP_DigestUpdate(md.get(), name.data(), name.size());
  EVP_DigestUpdate(md.get(), config.data(), config.size());

  // Silence unused parameter warnings.
  (void)hash_buffer;
  (void)hash_length;
}

absl::optional<uint32_t> DynamicModuleCertValidator::daysUntilFirstCertExpires() const {
  return absl::nullopt;
}

std::string DynamicModuleCertValidator::getCaFileName() const { return ""; }

Envoy::Ssl::CertificateDetailsPtr DynamicModuleCertValidator::getCaCertInformation() const {
  return nullptr;
}

// Factory implementation.

absl::StatusOr<CertValidatorPtr> DynamicModuleCertValidatorFactory::createCertValidator(
    const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
    Server::Configuration::CommonFactoryContext& /*context*/, Stats::Scope& /*scope*/) {
  ASSERT(config != nullptr);
  ASSERT(config->customValidatorConfig().has_value());

  // Parse the dynamic module cert validator config from the typed_config.
  envoy::extensions::transport_sockets::tls::cert_validator::dynamic_modules::v3::
      DynamicModuleCertValidatorConfig proto_config;
  auto status = Config::Utility::translateOpaqueConfig(
      config->customValidatorConfig().value().typed_config(),
      ProtobufMessage::getStrictValidationVisitor(), proto_config);
  RETURN_IF_NOT_OK(status);

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return dynamic_module.status();
  }

  std::string validator_config_str;
  if (proto_config.has_validator_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.validator_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    validator_config_str = std::move(config_or_error.value());
  }

  auto factory_config_or_error = newDynamicModuleCertValidatorConfig(
      proto_config.validator_name(), validator_config_str, std::move(dynamic_module.value()));
  if (!factory_config_or_error.ok()) {
    return factory_config_or_error.status();
  }

  return std::make_unique<DynamicModuleCertValidator>(std::move(factory_config_or_error.value()),
                                                      stats);
}

REGISTER_FACTORY(DynamicModuleCertValidatorFactory, CertValidatorFactory);

} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
