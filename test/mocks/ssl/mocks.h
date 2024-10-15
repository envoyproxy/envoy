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

#include "test/mocks/secret/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Ssl {

class MockContextManager : public ContextManager {
public:
  MockContextManager();
  ~MockContextManager() override;

  MOCK_METHOD(absl::StatusOr<ClientContextSharedPtr>, createSslClientContext,
              (Stats::Scope & scope, const ClientContextConfig& config));
  MOCK_METHOD(absl::StatusOr<ServerContextSharedPtr>, createSslServerContext,
              (Stats::Scope & stats, const ServerContextConfig& config,
               const std::vector<std::string>& server_names,
               ContextAdditionalInitFunc additional_init));
  MOCK_METHOD(absl::optional<uint32_t>, daysUntilFirstCertExpires, (), (const));
  MOCK_METHOD(absl::optional<uint64_t>, secondsUntilFirstOcspResponseExpires, (), (const));
  MOCK_METHOD(void, iterateContexts, (std::function<void(const Context&)> callback));
  MOCK_METHOD(Ssl::PrivateKeyMethodManager&, privateKeyMethodManager, ());
  MOCK_METHOD(void, removeContext, (const Envoy::Ssl::ContextSharedPtr& old_context));
};

class MockConnectionInfo : public ConnectionInfo {
public:
  MockConnectionInfo();
  ~MockConnectionInfo() override;

  MOCK_METHOD(bool, peerCertificatePresented, (), (const));
  MOCK_METHOD(bool, peerCertificateValidated, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, uriSanLocalCertificate, (), (const));
  MOCK_METHOD(const std::string&, sha256PeerCertificateDigest, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, sha256PeerCertificateChainDigests, (), (const));
  MOCK_METHOD(const std::string&, sha1PeerCertificateDigest, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, sha1PeerCertificateChainDigests, (), (const));
  MOCK_METHOD(const std::string&, serialNumberPeerCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, serialNumbersPeerCertificates, (), (const));
  MOCK_METHOD(const std::string&, issuerPeerCertificate, (), (const));
  MOCK_METHOD(const std::string&, subjectPeerCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, uriSanPeerCertificate, (), (const));
  MOCK_METHOD(const std::string&, subjectLocalCertificate, (), (const));
  MOCK_METHOD(const std::string&, urlEncodedPemEncodedPeerCertificate, (), (const));
  MOCK_METHOD(const std::string&, urlEncodedPemEncodedPeerCertificateChain, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, dnsSansPeerCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, dnsSansLocalCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, ipSansPeerCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, ipSansLocalCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, oidsPeerCertificate, (), (const));
  MOCK_METHOD(absl::Span<const std::string>, oidsLocalCertificate, (), (const));
  MOCK_METHOD(absl::optional<SystemTime>, validFromPeerCertificate, (), (const));
  MOCK_METHOD(absl::optional<SystemTime>, expirationPeerCertificate, (), (const));
  MOCK_METHOD(const std::string&, sessionId, (), (const));
  MOCK_METHOD(uint16_t, ciphersuiteId, (), (const));
  MOCK_METHOD(std::string, ciphersuiteString, (), (const));
  MOCK_METHOD(const std::string&, tlsVersion, (), (const));
  MOCK_METHOD(const std::string&, alpn, (), (const));
  MOCK_METHOD(const std::string&, sni, (), (const));
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext() override;

  MOCK_METHOD(absl::optional<uint32_t>, daysUntilFirstCertExpires, (), (const));
  MOCK_METHOD(absl::optional<uint64_t>, secondsUntilFirstOcspResponseExpires, (), (const));
  MOCK_METHOD(CertificateDetailsPtr, getCaCertInformation, (), (const));
  MOCK_METHOD(std::vector<CertificateDetailsPtr>, getCertChainInformation, (), (const));
};

class MockClientContextConfig : public ClientContextConfig {
public:
  MockClientContextConfig();
  ~MockClientContextConfig() override;

  MOCK_METHOD(const std::string&, alpnProtocols, (), (const));
  MOCK_METHOD(const std::string&, cipherSuites, (), (const));
  MOCK_METHOD(const std::string&, ecdhCurves, (), (const));
  MOCK_METHOD(const std::string&, signatureAlgorithms, (), (const));
  MOCK_METHOD(std::vector<std::reference_wrapper<const TlsCertificateConfig>>, tlsCertificates, (),
              (const));
  MOCK_METHOD(const CertificateValidationContextConfig*, certificateValidationContext, (), (const));
  MOCK_METHOD(unsigned, minProtocolVersion, (), (const));
  MOCK_METHOD(unsigned, maxProtocolVersion, (), (const));
  MOCK_METHOD(bool, isReady, (), (const));
  MOCK_METHOD(void, setSecretUpdateCallback, (std::function<absl::Status()> callback));

  MOCK_METHOD(Ssl::HandshakerFactoryCb, createHandshaker, (), (const, override));
  MOCK_METHOD(Ssl::HandshakerCapabilities, capabilities, (), (const, override));
  MOCK_METHOD(Ssl::SslCtxCb, sslctxCb, (), (const, override));

  MOCK_METHOD(const std::string&, serverNameIndication, (), (const));
  MOCK_METHOD(bool, allowRenegotiation, (), (const));
  MOCK_METHOD(bool, enforceRsaKeyUsage, (), (const));
  MOCK_METHOD(size_t, maxSessionKeys, (), (const));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogLocal, (), (const));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogRemote, (), (const));
  MOCK_METHOD(const std::string&, tlsKeyLogPath, (), (const));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (const));
  Ssl::HandshakerCapabilities capabilities_;
  std::string sni_{"default_sni.example.com"};
  std::string ciphers_{"RSA"};
  std::string alpn_{""};
  std::string sigalgs_{""};
  Network::Address::IpList iplist_;
  std::string path_{};
};

class MockServerContextConfig : public ServerContextConfig {
public:
  MockServerContextConfig();
  ~MockServerContextConfig() override;

  MOCK_METHOD(const std::string&, alpnProtocols, (), (const));
  MOCK_METHOD(const std::string&, cipherSuites, (), (const));
  MOCK_METHOD(const std::string&, ecdhCurves, (), (const));
  MOCK_METHOD(const std::string&, signatureAlgorithms, (), (const));
  MOCK_METHOD(bool, preferClientCiphers, (), (const));
  MOCK_METHOD(std::vector<std::reference_wrapper<const TlsCertificateConfig>>, tlsCertificates, (),
              (const));
  MOCK_METHOD(const CertificateValidationContextConfig*, certificateValidationContext, (), (const));
  MOCK_METHOD(unsigned, minProtocolVersion, (), (const));
  MOCK_METHOD(unsigned, maxProtocolVersion, (), (const));
  MOCK_METHOD(bool, isReady, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::seconds>, sessionTimeout, (), (const));
  MOCK_METHOD(void, setSecretUpdateCallback, (std::function<absl::Status()> callback));

  MOCK_METHOD(Ssl::HandshakerFactoryCb, createHandshaker, (), (const, override));
  MOCK_METHOD(Ssl::TlsCertificateSelectorFactory, tlsCertificateSelectorFactory, (),
              (const, override));
  MOCK_METHOD(Ssl::HandshakerCapabilities, capabilities, (), (const, override));
  MOCK_METHOD(Ssl::SslCtxCb, sslctxCb, (), (const, override));

  MOCK_METHOD(bool, requireClientCertificate, (), (const));
  MOCK_METHOD(OcspStaplePolicy, ocspStaplePolicy, (), (const));
  MOCK_METHOD(const std::vector<SessionTicketKey>&, sessionTicketKeys, (), (const));
  MOCK_METHOD(bool, disableStatelessSessionResumption, (), (const));
  MOCK_METHOD(bool, disableStatefulSessionResumption, (), (const));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogLocal, (), (const));
  MOCK_METHOD(const Network::Address::IpList&, tlsKeyLogRemote, (), (const));
  MOCK_METHOD(const std::string&, tlsKeyLogPath, (), (const));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (const));
  MOCK_METHOD(bool, fullScanCertsOnSNIMismatch, (), (const));

  Ssl::HandshakerCapabilities capabilities_;
  std::string ciphers_{"RSA"};
  std::string alpn_{""};
  std::string sigalgs_{""};
  Network::Address::IpList iplist_;
  std::string path_;
  std::vector<SessionTicketKey> ticket_keys_;
};

class MockTlsCertificateConfig : public TlsCertificateConfig {
public:
  MockTlsCertificateConfig() = default;
  ~MockTlsCertificateConfig() override = default;

  MOCK_METHOD(const std::string&, certificateChain, (), (const));
  MOCK_METHOD(const std::string&, certificateChainPath, (), (const));
  MOCK_METHOD(const std::string&, pkcs12, (), (const));
  MOCK_METHOD(const std::string&, pkcs12Path, (), (const));
  MOCK_METHOD(const std::string&, privateKey, (), (const));
  MOCK_METHOD(const std::string&, privateKeyPath, (), (const));
  MOCK_METHOD(const std::vector<uint8_t>&, ocspStaple, (), (const));
  MOCK_METHOD(const std::string&, ocspStaplePath, (), (const));
  MOCK_METHOD(const std::string&, password, (), (const));
  MOCK_METHOD(const std::string&, passwordPath, (), (const));
  MOCK_METHOD(Envoy::Ssl::PrivateKeyMethodProviderSharedPtr, privateKeyMethod, (), (const));
};

class MockCertificateValidationContextConfig : public CertificateValidationContextConfig {
public:
  MOCK_METHOD(const std::string&, caCert, (), (const));
  MOCK_METHOD(const std::string&, caCertPath, (), (const));
  MOCK_METHOD(const std::string&, certificateRevocationList, (), (const));
  MOCK_METHOD(const std::string&, certificateRevocationListPath, (), (const));
  MOCK_METHOD(
      const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&,
      subjectAltNameMatchers, (), (const));
  MOCK_METHOD(const std::vector<std::string>&, verifyCertificateHashList, (), (const));
  MOCK_METHOD(const std::vector<std::string>&, verifyCertificateSpkiList, (), (const));
  MOCK_METHOD(bool, allowExpiredCertificate, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&,
              customValidatorConfig, (), (const));
  MOCK_METHOD(Api::Api&, api, (), (const));
  MOCK_METHOD(envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                  TrustChainVerification,
              trustChainVerification, (), (const));
  MOCK_METHOD(bool, onlyVerifyLeafCertificateCrl, (), (const));
  MOCK_METHOD(absl::optional<uint32_t>, maxVerifyDepth, (), (const));
};

class MockPrivateKeyMethodManager : public PrivateKeyMethodManager {
public:
  MockPrivateKeyMethodManager();
  ~MockPrivateKeyMethodManager() override;

  MOCK_METHOD(PrivateKeyMethodProviderSharedPtr, createPrivateKeyMethodProvider,
              (const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
               Envoy::Server::Configuration::TransportSocketFactoryContext& factory_context));
};

class MockPrivateKeyMethodProvider : public PrivateKeyMethodProvider {
public:
  MockPrivateKeyMethodProvider();
  ~MockPrivateKeyMethodProvider() override;

  MOCK_METHOD(void, registerPrivateKeyMethod,
              (SSL * ssl, PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher));
  MOCK_METHOD(void, unregisterPrivateKeyMethod, (SSL * ssl));
  MOCK_METHOD(bool, checkFips, ());
  MOCK_METHOD(bool, isAvailable, ());

#ifdef OPENSSL_IS_BORINGSSL
  MOCK_METHOD(BoringSslPrivateKeyMethodSharedPtr, getBoringSslPrivateKeyMethod, ());
#endif
};

} // namespace Ssl
} // namespace Envoy
