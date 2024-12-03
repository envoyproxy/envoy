#pragma once

#include "source/common/common/macros.h"
#include "source/common/common/posix/thread_impl.h"
#include "source/common/common/thread.h"
#include "source/common/tls/cert_validator/default_validator.h"

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
                              SslStats& stats);

  ~PlatformBridgeCertValidator() override;

  // CertValidator
  // These are not very meaningful interfaces for cert validator on client side and are only called
  // by server TLS context. Ideally they should be moved from CertValidator into an extended server
  // cert validator interface. And this class only extends the client interface. But their owner
  // (Tls::ContextImpl) doesn't have endpoint perspective today, so there will need more refactoring
  // to achieve this.
  absl::Status addClientValidationContext(SSL_CTX* /*context*/,
                                          bool /*require_client_cert*/) override {
    IS_ENVOY_BUG("Should not be reached");
    return absl::InvalidArgumentError("unexpected call");
  }
  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& /*md*/,
                                uint8_t* /*hash_buffer[EVP_MAX_MD_SIZE]*/,
                                unsigned /*hash_length*/) override {
    IS_ENVOY_BUG("Should not be reached");
  }
  absl::optional<uint32_t> daysUntilFirstCertExpires() const override { return absl::nullopt; }
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override { return nullptr; }
  // Return empty string
  std::string getCaFileName() const override { return ""; }
  // Overridden to call into platform extension API asynchronously.
  ValidationResults
  doVerifyCertChain(STACK_OF(X509) & cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view hostname) override;
  // Returns SSL_VERIFY_PEER so that doVerifyCertChain() will be called from the TLS stack.
  absl::StatusOr<int> initializeSslContexts(std::vector<SSL_CTX*> /*contexts*/,
                                            bool /*handshaker_provides_certificates*/) override {
    return SSL_VERIFY_PEER;
  }

protected:
  enum class ValidationFailureType {
    Success,
    FailVerifyError,
    FailVerifySan,
  };

  // Calls into platform APIs in a stand-alone thread to verify the given certs.
  // Once the validation is done, the result will be posted back to the current
  // thread to trigger callback and update verify stats.
  // Must be called on the validation thread.
  //
  // `protected` for testing purposes.
  virtual void verifyCertChainByPlatform(Event::Dispatcher* dispatcher,
                                         std::vector<std::string> cert_chain, std::string hostname,
                                         std::vector<std::string> subject_alt_names);

  // Must be called on the validation thread.
  // `protected` for testing purposes.
  static void postVerifyResultAndCleanUp(bool success, std::string hostname,
                                         absl::string_view error_details, uint8_t tls_alert,
                                         ValidationFailureType failure_type,
                                         Event::Dispatcher* dispatcher,
                                         PlatformBridgeCertValidator* parent);

private:
  GTEST_FRIEND_CLASS(PlatformBridgeCertValidatorTest, ThreadCreationFailed);

  PlatformBridgeCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                              SslStats& stats, Thread::PosixThreadFactoryPtr thread_factory);

  // Called when a pending verification completes. Must be invoked on the main thread.
  void onVerificationComplete(const Thread::ThreadId& thread_id, const std::string& hostname,
                              bool success, const std::string& error_details, uint8_t tls_alert,
                              ValidationFailureType failure_type);

  struct ValidationJob {
    Ssl::ValidateResultCallbackPtr result_callback_;
    Thread::PosixThreadPtr validation_thread_;
  };

  const bool allow_untrusted_certificate_;
  SslStats& stats_;
  absl::flat_hash_map<Thread::ThreadId, ValidationJob> validation_jobs_;
  std::shared_ptr<size_t> alive_indicator_{new size_t(1)};
  Thread::PosixThreadFactoryPtr thread_factory_;
  absl::optional<int> thread_priority_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
