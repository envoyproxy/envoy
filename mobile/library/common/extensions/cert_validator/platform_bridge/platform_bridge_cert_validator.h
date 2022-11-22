#pragma once

#include <thread>

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"

#include "absl/container/flat_hash_map.h"
#include "library/common/extensions/cert_validator/platform_bridge/c_types.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// A certification validation implementation that uses the platform provided APIs to verify
// certificate chain. Since some platform APIs are slow blocking calls, in order not to block
// network events, this implementation creates stand-alone threads to make those calls for each
// validation.
class PlatformBridgeCertValidator : public CertValidator, Logger::Loggable<Logger::Id::connection> {
public:
  PlatformBridgeCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                              SslStats& stats, const envoy_cert_validator* platform_validator);

  ~PlatformBridgeCertValidator() override;

  // CertValidator
  // These are not very meaningful interfaces for cert validator on client side and are only called
  // by server TLS context. Ideally they should be moved from CertValidator into an extended server
  // cert validator interface. And this class only extends the client interface. But their owner
  // (Tls::ContextImpl) doesn't have endpoint perspective today, so there will need more refactoring
  // to achieve this.
  void addClientValidationContext(SSL_CTX* /*context*/, bool /*require_client_cert*/) override {
    PANIC("Should not be reached");
  }
  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& /*md*/,
                                uint8_t* /*hash_buffer[EVP_MAX_MD_SIZE]*/,
                                unsigned /*hash_length*/) override {
    PANIC("Should not be reached");
  }
  int doSynchronousVerifyCertChain(
      X509_STORE_CTX* /*store_ctx*/,
      Ssl::SslExtendedSocketInfo*
      /*ssl_extended_info*/,
      X509& /*leaf_cert*/,
      const Network::TransportSocketOptions* /*transport_socket_options*/) override {
    PANIC("Should not be reached");
  }
  absl::optional<uint32_t> daysUntilFirstCertExpires() const override { return absl::nullopt; }
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override { return nullptr; }
  // Return empty string
  std::string getCaFileName() const override { return ""; }
  // Overridden to call into platform extension API asynchronously.
  ValidationResults
  doVerifyCertChain(STACK_OF(X509) & cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    Ssl::SslExtendedSocketInfo* ssl_extended_info,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;
  // As CA path will not be configured, make sure the return value won’t be SSL_VERIFY_NONE because
  // of that, so that doVerifyCertChain() will be called from the TLS stack.
  int initializeSslContexts(std::vector<SSL_CTX*> contexts,
                            bool handshaker_provides_certificates) override;

private:
  class PendingValidation {
  public:
    PendingValidation(PlatformBridgeCertValidator& parent, std::vector<envoy_data> certs,
                      absl::string_view host_name,
                      const Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                      Ssl::ValidateResultCallbackPtr result_callback)
        : parent_(parent), certs_(std::move(certs)), host_name_(host_name),
          result_callback_(std::move(result_callback)),
          transport_socket_options_(std::move(transport_socket_options)) {}

    void verifyCertsByPlatform();

    void postVerifyResultAndCleanUp(bool success, absl::string_view error_details,
                                    uint8_t tls_alert, OptRef<Stats::Counter> error_counter);

    struct Hash {
      size_t operator()(const PendingValidation& p) const {
        return reinterpret_cast<size_t>(p.result_callback_.get());
      }
    };
    struct Eq {
      bool operator()(const PendingValidation& a, const PendingValidation& b) const {
        return a.result_callback_.get() == b.result_callback_.get();
      }
    };

  private:
    Event::SchedulableCallbackPtr next_iteration_callback_;
    PlatformBridgeCertValidator& parent_;
    std::vector<envoy_data> certs_;
    std::string host_name_;
    Ssl::ValidateResultCallbackPtr result_callback_;
    const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  };

  // Calls into platform APIs in a stand-alone thread to verify the given certs.
  // Once the validation is done, the result will be posted back to the current
  // thread to trigger callback and update verify stats.
  void verifyCertChainByPlatform(std::vector<envoy_data>& cert_chain, const std::string& host_name,
                                 const std::vector<std::string>& subject_alt_names,
                                 PendingValidation& pending_validation);

  const Envoy::Ssl::CertificateValidationContextConfig* config_;
  SslStats& stats_;
  bool allow_untrusted_certificate_ = false;
  // latches the platform extension API.
  const envoy_cert_validator* platform_validator_;
  absl::flat_hash_map<std::thread::id, std::thread> validation_threads_;
  absl::flat_hash_set<PendingValidation, PendingValidation::Hash, PendingValidation::Eq>
      validations_;
  std::shared_ptr<size_t> alive_indicator_{new size_t(1)};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
