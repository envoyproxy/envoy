#pragma once

#include <functional>
#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/connection.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/tls/cert_validator/san_matcher.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Ssl {

class MockContextManager : public ContextManager {
public:
  MockContextManager();
  ~MockContextManager() override;

  MOCK_METHOD(absl::StatusOr<ClientContextSharedPtr>, createSslClientContext,
              (Stats::Scope & scope, const ClientContextConfig& config), (override));
  MOCK_METHOD(absl::StatusOr<ServerContextSharedPtr>, createSslServerContext,
              (Stats::Scope & stats, const ServerContextConfig& config,
               ContextAdditionalInitFunc additional_init),
              (override));
  MOCK_METHOD(std::optional<uint32_t>, daysUntilFirstCertExpires, (), (const, override));
  MOCK_METHOD(std::optional<uint64_t>, secondsUntilFirstOcspResponseExpires, (), (const, override));
  MOCK_METHOD(void, iterateContexts, (std::function<void(const Context&)> callback), (override));
  MOCK_METHOD(Ssl::PrivateKeyMethodManager&, privateKeyMethodManager, (), (override));
  MOCK_METHOD(void, removeContext, (const Envoy::Ssl::ContextSharedPtr& old_context), (override));
};

class MockConnectionInfo : public ConnectionInfo {
public:
  MockConnectionInfo();
  ~MockConnectionInfo() override;

  MOCK_METHOD(bool, peerCertificatePresented, (), (const, override));
  MOCK_METHOD(bool, peerCertificateValidated, (), (const, override));
  MOCK_METHOD(const std::string&, sha256PeerCertificateDigest, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, sha256PeerCertificateChainDigests, (),
              (const, override));
  MOCK_METHOD(const std::string&, sha1PeerCertificateDigest, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, sha1PeerCertificateChainDigests, (),
              (const, override));
  MOCK_METHOD(const std::string&, serialNumberPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, serialNumbersPeerCertificates, (), (const, override));
  MOCK_METHOD(const std::string&, issuerPeerCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, sha256PeerCertificateIssuerDigest, (), (const, override));
  MOCK_METHOD(const std::string&, serialNumberPeerCertificateIssuer, (), (const, override));
  MOCK_METHOD(const std::string&, subjectPeerCertificate, (), (const, override));
  MOCK_METHOD(ParsedX509NameOptConstRef, parsedSubjectPeerCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, subjectLocalCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, urlEncodedPemEncodedPeerCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, pemEncodedPeerCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, urlEncodedPemEncodedPeerCertificateChain, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, pemEncodedPeerCertificateChain, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, pemEncodedValidatedPeerCertificateChain, (),
              (const, override));
  MOCK_METHOD(bool, peerCertificateSanMatches, (const Ssl::SanMatcher&), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, uriSanPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, uriSanLocalCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, dnsSansPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, dnsSansLocalCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, ipSansPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, ipSansLocalCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, emailSansPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, emailSansLocalCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, othernameSansPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, othernameSansLocalCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, oidsPeerCertificate, (), (const, override));
  MOCK_METHOD(absl::Span<const std::string>, oidsLocalCertificate, (), (const, override));
  MOCK_METHOD(std::optional<SystemTime>, validFromPeerCertificate, (), (const, override));
  MOCK_METHOD(std::optional<SystemTime>, expirationPeerCertificate, (), (const, override));
  MOCK_METHOD(const std::string&, sessionId, (), (const, override));
  MOCK_METHOD(uint16_t, ciphersuiteId, (), (const, override));
  MOCK_METHOD(absl::string_view, ciphersuiteString, (), (const, override));
  MOCK_METHOD(uint16_t, tlsGroupId, (), (const, override));
  MOCK_METHOD(absl::string_view, tlsGroupString, (), (const, override));
  MOCK_METHOD(const std::string&, tlsVersion, (), (const, override));
  MOCK_METHOD(const std::string&, alpn, (), (const, override));
  MOCK_METHOD(const std::string&, sni, (), (const, override));
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext() override;

  MOCK_METHOD(std::optional<uint32_t>, daysUntilFirstCertExpires, (), (const, override));
  MOCK_METHOD(std::optional<uint64_t>, secondsUntilFirstOcspResponseExpires, (), (const, override));
  MOCK_METHOD(std::vector<CertificateDetailsPtr>, getCaCertInformation, (), (const, override));
  MOCK_METHOD(std::vector<CertificateDetailsPtr>, getCertChainInformation, (), (const, override));
  MOCK_METHOD(const TlsContext&, getTlsContext, (), (const, override));

private:
  // Default return value for getTlsContext(): an empty context with no certificate.
  std::unique_ptr<TlsContext> default_tls_context_;
};

class MockClientContextConfig : public ClientContextConfig {
public:
  MockClientContextConfig();
  ~MockClientContextConfig() override;

  MOCK_METHOD(const std::string&, alpnProtocols, (), (const, override));
  MOCK_METHOD(const std::string&, cipherSuites, (), (const, override));
  MOCK_METHOD(const std::string&, ecdhCurves, (), (const, override));
  MOCK_METHOD(const std::string&, signatureAlgorithms, (), (const, override));
  MOCK_METHOD(std::vector<std::reference_wrapper<const TlsCertificateConfig>>, tlsCertificates, (),
              (const, override));
  MOCK_METHOD(const CertificateValidationContextConfig*, certificateValidationContext, (),
              (const, override));
  MOCK_METHOD(bool, validationContextConfigured, (), (const, override));
  MOCK_METHOD(unsigned, minProtocolVersion, (), (const, override));
  MOCK_METHOD(unsigned, maxProtocolVersion, (), (const, override));
  MOCK_METHOD(bool, isReady, (), (const, override));
  MOCK_METHOD(void, setSecretUpdateCallback, (std::function<absl::Status()> callback), (override));

  MOCK_METHOD(Ssl::HandshakerFactoryCb, createHandshaker, (), (const, override));
  MOCK_METHOD(Ssl::HandshakerCapabilities, capabilities, (), (const, override));
  MOCK_METHOD(Ssl::SslCtxCb, sslctxCb, (), (const, override));

  MOCK_METHOD(const std::string&, serverNameIndication, (), (const, override));
  MOCK_METHOD(bool, autoHostServerNameIndication, (), (const, override));
  MOCK_METHOD(bool, autoSniSanMatch, (), (const, override));
  MOCK_METHOD(bool, allowRenegotiation, (), (const, override));

  MOCK_METHOD(size_t, maxSessionKeys, (), (const, override));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogLocal, (), (const, override));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogRemote, (), (const, override));
  MOCK_METHOD(const std::string&, tlsKeyLogPath, (), (const, override));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (const, override));
  MOCK_METHOD(
      std::optional<envoy::extensions::transport_sockets::tls::v3::TlsParameters::CompliancePolicy>,
      compliancePolicy, (), (const, override));
  MOCK_METHOD(OptRef<Ssl::UpstreamTlsCertificateSelectorFactory>, tlsCertificateSelectorFactory, (),
              (const, override));
  Ssl::HandshakerCapabilities capabilities_;
  std::string sni_{"default_sni.example.com"};
  std::string ciphers_{"RSA"};
  std::string curves_{""};
  std::string alpn_{""};
  std::string sigalgs_{""};
  Network::Address::IpList iplist_;
  std::string path_;
};

class MockServerContextConfig : public ServerContextConfig {
public:
  MockServerContextConfig();
  ~MockServerContextConfig() override;

  MOCK_METHOD(const std::string&, alpnProtocols, (), (const, override));
  MOCK_METHOD(const std::string&, cipherSuites, (), (const, override));
  MOCK_METHOD(const std::string&, ecdhCurves, (), (const, override));
  MOCK_METHOD(const std::string&, signatureAlgorithms, (), (const, override));
  MOCK_METHOD(bool, preferClientCiphers, (), (const, override));
  MOCK_METHOD(std::vector<std::reference_wrapper<const TlsCertificateConfig>>, tlsCertificates, (),
              (const, override));
  MOCK_METHOD(const CertificateValidationContextConfig*, certificateValidationContext, (),
              (const, override));
  MOCK_METHOD(bool, validationContextConfigured, (), (const, override));
  MOCK_METHOD(unsigned, minProtocolVersion, (), (const, override));
  MOCK_METHOD(unsigned, maxProtocolVersion, (), (const, override));
  MOCK_METHOD(bool, isReady, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::seconds>, sessionTimeout, (), (const, override));
  MOCK_METHOD(void, setSecretUpdateCallback, (std::function<absl::Status()> callback), (override));

  MOCK_METHOD(Ssl::HandshakerFactoryCb, createHandshaker, (), (const, override));
  MOCK_METHOD(Ssl::TlsCertificateSelectorFactory&, tlsCertificateSelectorFactory, (),
              (const, override));
  MOCK_METHOD(Ssl::HandshakerCapabilities, capabilities, (), (const, override));
  MOCK_METHOD(Ssl::SslCtxCb, sslctxCb, (), (const, override));

  MOCK_METHOD(bool, requireClientCertificate, (), (const, override));
  MOCK_METHOD(OcspStaplePolicy, ocspStaplePolicy, (), (const, override));
  MOCK_METHOD(const std::vector<SessionTicketKey>&, sessionTicketKeys, (), (const, override));
  MOCK_METHOD(bool, disableStatelessSessionResumption, (), (const, override));
  MOCK_METHOD(bool, disableStatefulSessionResumption, (), (const, override));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogLocal, (), (const, override));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogRemote, (), (const, override));
  MOCK_METHOD(const std::string&, tlsKeyLogPath, (), (const, override));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (const, override));
  MOCK_METHOD(bool, fullScanCertsOnSNIMismatch, (), (const, override));
  MOCK_METHOD(
      std::optional<envoy::extensions::transport_sockets::tls::v3::TlsParameters::CompliancePolicy>,
      compliancePolicy, (), (const, override));
  MOCK_METHOD(const std::vector<std::string>&, serverNames, (), (const, override));

  Ssl::HandshakerCapabilities capabilities_;
  std::string ciphers_{"RSA"};
  std::string curves_{""};
  std::string alpn_{""};
  std::string sigalgs_{""};
  Network::Address::IpList iplist_;
  std::string path_;
  std::vector<SessionTicketKey> ticket_keys_;
  std::vector<std::string> server_names_;
};

class MockTlsCertificateConfig : public TlsCertificateConfig {
public:
  MockTlsCertificateConfig() = default;
  ~MockTlsCertificateConfig() override = default;

  MOCK_METHOD(const std::string&, certificateChain, (), (const, override));
  MOCK_METHOD(const std::string&, certificateChainPath, (), (const, override));
  MOCK_METHOD(const std::string&, certificateName, (), (const, override));
  MOCK_METHOD(const std::string&, pkcs12, (), (const, override));
  MOCK_METHOD(const std::string&, pkcs12Path, (), (const, override));
  MOCK_METHOD(const std::string&, privateKey, (), (const, override));
  MOCK_METHOD(const std::string&, privateKeyPath, (), (const, override));
  MOCK_METHOD(const std::vector<uint8_t>&, ocspStaple, (), (const, override));
  MOCK_METHOD(const std::string&, ocspStaplePath, (), (const, override));
  MOCK_METHOD(const std::string&, password, (), (const, override));
  MOCK_METHOD(const std::string&, passwordPath, (), (const, override));
  MOCK_METHOD(Envoy::Ssl::PrivateKeyMethodProviderSharedPtr, privateKeyMethod, (),
              (const, override));
  MOCK_METHOD(const Envoy::Ssl::TlsParams*, tlsParams, (), (const, override));
};

class MockCertificateValidationContextConfig : public CertificateValidationContextConfig {
public:
  MOCK_METHOD(const std::string&, caCert, (), (const, override));
  MOCK_METHOD(const std::string&, caCertPath, (), (const, override));
  MOCK_METHOD(const std::string&, caCertName, (), (const, override));
  MOCK_METHOD(const std::string&, certificateRevocationList, (), (const, override));
  MOCK_METHOD(const std::string&, certificateRevocationListPath, (), (const, override));
  MOCK_METHOD(
      const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&,
      subjectAltNameMatchers, (), (const, override));
  MOCK_METHOD(const std::vector<std::string>&, verifyCertificateHashList, (), (const, override));
  MOCK_METHOD(const std::vector<std::string>&, verifyCertificateSpkiList, (), (const, override));
  MOCK_METHOD(bool, allowExpiredCertificate, (), (const, override));
  MOCK_METHOD(const std::optional<envoy::config::core::v3::TypedExtensionConfig>&,
              customValidatorConfig, (), (const, override));
  MOCK_METHOD(Api::Api&, api, (), (const, override));
  MOCK_METHOD(envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                  TrustChainVerification,
              trustChainVerification, (), (const, override));
  MOCK_METHOD(bool, onlyVerifyLeafCertificateCrl, (), (const, override));
  MOCK_METHOD(std::optional<uint32_t>, maxVerifyDepth, (), (const, override));
  MOCK_METHOD(bool, autoSniSanMatch, (), (const, override));
  MOCK_METHOD(bool, suppressClientCaList, (), (const, override));
};

class MockPrivateKeyMethodManager : public PrivateKeyMethodManager {
public:
  MockPrivateKeyMethodManager();
  ~MockPrivateKeyMethodManager() override;

  MOCK_METHOD(PrivateKeyMethodProviderSharedPtr, createPrivateKeyMethodProvider,
              (const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
               Envoy::Server::Configuration::TransportSocketFactoryContext& factory_context),
              (override));
};

class MockPrivateKeyMethodProvider : public PrivateKeyMethodProvider {
public:
  MockPrivateKeyMethodProvider();
  ~MockPrivateKeyMethodProvider() override;

  MOCK_METHOD(void, registerPrivateKeyMethod,
              (SSL * ssl, PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher),
              (override));
  MOCK_METHOD(void, unregisterPrivateKeyMethod, (SSL * ssl), (override));
  MOCK_METHOD(bool, checkFips, (), (override));
  MOCK_METHOD(bool, isAvailable, (), (override));

#ifdef OPENSSL_IS_BORINGSSL
  MOCK_METHOD(BoringSslPrivateKeyMethodSharedPtr, getBoringSslPrivateKeyMethod, (), (override));
#endif
};

} // namespace Ssl
} // namespace Envoy
