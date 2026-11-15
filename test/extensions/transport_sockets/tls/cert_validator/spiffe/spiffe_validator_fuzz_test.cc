#include <tuple>

#include "envoy/extensions/transport_sockets/tls/v3/common.pb.validate.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/stats.h"
#include "source/extensions/transport_sockets/tls/cert_validator/spiffe/spiffe_validator.h"

#include "test/common/tls/cert_validator/test_common.h"
#include "test/extensions/transport_sockets/tls/cert_validator/spiffe/spiffe_validator_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "openssl/ssl.h"
#include "openssl/x509.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

using test::extensions::transport_sockets::tls::cert_validator::spiffe::SpiffeValidatorFuzzTestCase;

constexpr absl::string_view WorkloadTrustDomainKey =
    "envoy.tls.cert_validator.spiffe.workload_trust_domain";

// 2030-01-01T00:00:00Z. Pinning the verification time keeps runs reproducible; the corpus
// certificates are valid from 2025 to 2125.
constexpr int64_t VerificationTime = 1893456000;

bool isInline(const envoy::config::core::v3::DataSource& source) {
  switch (source.specifier_case()) {
  case envoy::config::core::v3::DataSource::kInlineBytes:
  case envoy::config::core::v3::DataSource::kInlineString:
  case envoy::config::core::v3::DataSource::SPECIFIER_NOT_SET:
    return true;
  default:
    return false;
  }
}

DEFINE_PROTO_FUZZER(const SpiffeValidatorFuzzTestCase& input) {
  if (!isInline(input.config().trust_bundles())) {
    return;
  }
  for (const auto& domain : input.config().trust_domains()) {
    if (!isInline(domain.trust_bundle())) {
      return;
    }
  }
  // The SPIFFE config itself is not validated on the production path, but the SAN matchers are,
  // as part of the enclosing CertificateValidationContext.
  try {
    for (const auto& matcher : input.san_matchers()) {
      TestUtility::validate(matcher);
    }
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  // Must precede the mocks below, which each pick up the process-wide test time system.
  Event::SimulatedTimeSystem time_system;
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  ON_CALL(context, api()).WillByDefault(testing::ReturnRef(*api));
  Stats::TestUtil::TestStore store;
  SslStats stats = generateSslStats(*store.rootScope());

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  typed_config.set_name("envoy.tls.cert_validator.spiffe");
  std::ignore = typed_config.mutable_typed_config()->PackFrom(input.config());
  TestCertificateValidationContextConfig config(
      typed_config, input.allow_expired_certificate(),
      {input.san_matchers().begin(), input.san_matchers().end()}, /*ca_cert=*/"",
      /*verify_depth=*/std::nullopt, input.suppress_client_ca_list());

  absl::Status creation_status = absl::OkStatus();
  SPIFFEValidator validator(&config, stats, context, *store.rootScope(), creation_status);
  if (!creation_status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to create validator: {}", creation_status);
    return;
  }

  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  validator.addClientValidationContext(ssl_ctx.get(), true).IgnoreError();
  bssl::ScopedEVP_MD_CTX md;
  EVP_DigestInit(md.get(), EVP_sha256());
  uint8_t hash_buffer[EVP_MAX_MD_SIZE];
  validator.updateDigestForSessionId(md, hash_buffer, SHA256_DIGEST_LENGTH);
  validator.daysUntilFirstCertExpires();
  validator.getCaCertInformation();

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  for (const std::string& der : input.cert_chain()) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(der.data());
    bssl::UniquePtr<X509> cert(d2i_X509(nullptr, &data, der.size()));
    // The TLS stack rejects a peer certificate that does not parse before any validator runs.
    if (cert == nullptr) {
      return;
    }
    sk_X509_push(cert_chain.get(), cert.release());
  }
  X509_VERIFY_PARAM_set_time(SSL_CTX_get0_param(ssl_ctx.get()), VerificationTime);

  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  CertValidator::ExtraValidationContext validation_context{};
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options;
  if (input.is_server()) {
    if (!input.workload_trust_domain().empty()) {
      callbacks.connection().streamInfo().filterState()->setData(
          WorkloadTrustDomainKey,
          std::make_shared<Router::StringAccessorImpl>(input.workload_trust_domain()),
          StreamInfo::FilterState::LifeSpan::Connection);
    }
    validation_context.callbacks = &callbacks;
  } else {
    auto filter_state_objects = std::make_unique<StreamInfo::FilterState::Objects>();
    if (!input.workload_trust_domain().empty()) {
      filter_state_objects->push_back(
          {std::make_shared<Router::StringAccessorImpl>(input.workload_trust_domain()),
           StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection,
           std::string(WorkloadTrustDomainKey)});
    }
    transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>(
        "",
        std::vector<std::string>{input.verify_subject_alt_names().begin(),
                                 input.verify_subject_alt_names().end()},
        std::vector<std::string>{}, std::vector<std::string>{}, std::nullopt,
        std::move(filter_state_objects));
  }

  const ValidationResults results =
      validator.doVerifyCertChain(*cert_chain, /*callback=*/nullptr, transport_socket_options,
                                  *ssl_ctx, validation_context, input.is_server(), "");
  ENVOY_LOG_MISC(debug, "Validation {}: {}",
                 results.status == ValidationResults::ValidationStatus::Successful ? "succeeded"
                                                                                   : "failed",
                 results.error_details.value_or(""));
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
