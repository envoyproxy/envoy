#pragma once

#include <functional>
#include <string>

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

  MOCK_METHOD2(createSslClientContext,
               ClientContextSharedPtr(Stats::Scope& scope, const ClientContextConfig& config));
  MOCK_METHOD3(createSslServerContext,
               ServerContextSharedPtr(Stats::Scope& stats, const ServerContextConfig& config,
                                      const std::vector<std::string>& server_names));
  MOCK_CONST_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_METHOD1(iterateContexts, void(std::function<void(const Context&)> callback));
  MOCK_METHOD0(privateKeyMethodManager, Ssl::PrivateKeyMethodManager&());
};

class MockConnectionInfo : public ConnectionInfo {
public:
  MockConnectionInfo();
  ~MockConnectionInfo() override;

  MOCK_CONST_METHOD0(peerCertificatePresented, bool());
  MOCK_CONST_METHOD0(uriSanLocalCertificate, absl::Span<const std::string>());
  MOCK_CONST_METHOD0(sha256PeerCertificateDigest, const std::string&());
  MOCK_CONST_METHOD0(serialNumberPeerCertificate, const std::string&());
  MOCK_CONST_METHOD0(issuerPeerCertificate, const std::string&());
  MOCK_CONST_METHOD0(subjectPeerCertificate, const std::string&());
  MOCK_CONST_METHOD0(uriSanPeerCertificate, absl::Span<const std::string>());
  MOCK_CONST_METHOD0(subjectLocalCertificate, const std::string&());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificate, const std::string&());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificateChain, const std::string&());
  MOCK_CONST_METHOD0(dnsSansPeerCertificate, absl::Span<const std::string>());
  MOCK_CONST_METHOD0(dnsSansLocalCertificate, absl::Span<const std::string>());
  MOCK_CONST_METHOD0(validFromPeerCertificate, absl::optional<SystemTime>());
  MOCK_CONST_METHOD0(expirationPeerCertificate, absl::optional<SystemTime>());
  MOCK_CONST_METHOD0(sessionId, const std::string&());
  MOCK_CONST_METHOD0(ciphersuiteId, uint16_t());
  MOCK_CONST_METHOD0(ciphersuiteString, std::string());
  MOCK_CONST_METHOD0(tlsVersion, const std::string&());
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext() override;

  MOCK_CONST_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_CONST_METHOD0(getCaCertInformation, CertificateDetailsPtr());
  MOCK_CONST_METHOD0(getCertChainInformation, std::vector<CertificateDetailsPtr>());
};

class MockClientContextConfig : public ClientContextConfig {
public:
  MockClientContextConfig();
  ~MockClientContextConfig() override;

  MOCK_CONST_METHOD0(alpnProtocols, const std::string&());
  MOCK_CONST_METHOD0(cipherSuites, const std::string&());
  MOCK_CONST_METHOD0(ecdhCurves, const std::string&());
  MOCK_CONST_METHOD0(tlsCertificates,
                     std::vector<std::reference_wrapper<const TlsCertificateConfig>>());
  MOCK_CONST_METHOD0(certificateValidationContext, const CertificateValidationContextConfig*());
  MOCK_CONST_METHOD0(minProtocolVersion, unsigned());
  MOCK_CONST_METHOD0(maxProtocolVersion, unsigned());
  MOCK_CONST_METHOD0(isReady, bool());
  MOCK_METHOD1(setSecretUpdateCallback, void(std::function<void()> callback));

  MOCK_CONST_METHOD0(serverNameIndication, const std::string&());
  MOCK_CONST_METHOD0(allowRenegotiation, bool());
  MOCK_CONST_METHOD0(maxSessionKeys, size_t());
  MOCK_CONST_METHOD0(signingAlgorithmsForTest, const std::string&());
};

class MockServerContextConfig : public ServerContextConfig {
public:
  MockServerContextConfig();
  ~MockServerContextConfig() override;

  MOCK_CONST_METHOD0(alpnProtocols, const std::string&());
  MOCK_CONST_METHOD0(cipherSuites, const std::string&());
  MOCK_CONST_METHOD0(ecdhCurves, const std::string&());
  MOCK_CONST_METHOD0(tlsCertificates,
                     std::vector<std::reference_wrapper<const TlsCertificateConfig>>());
  MOCK_CONST_METHOD0(certificateValidationContext, const CertificateValidationContextConfig*());
  MOCK_CONST_METHOD0(minProtocolVersion, unsigned());
  MOCK_CONST_METHOD0(maxProtocolVersion, unsigned());
  MOCK_CONST_METHOD0(isReady, bool());
  MOCK_METHOD1(setSecretUpdateCallback, void(std::function<void()> callback));

  MOCK_CONST_METHOD0(requireClientCertificate, bool());
  MOCK_CONST_METHOD0(sessionTicketKeys, const std::vector<SessionTicketKey>&());
};

class MockPrivateKeyMethodManager : public PrivateKeyMethodManager {
public:
  MockPrivateKeyMethodManager();
  ~MockPrivateKeyMethodManager() override;

  MOCK_METHOD2(createPrivateKeyMethodProvider,
               PrivateKeyMethodProviderSharedPtr(
                   const envoy::api::v2::auth::PrivateKeyProvider& config,
                   Envoy::Server::Configuration::TransportSocketFactoryContext& factory_context));
};

class MockPrivateKeyMethodProvider : public PrivateKeyMethodProvider {
public:
  MockPrivateKeyMethodProvider();
  ~MockPrivateKeyMethodProvider() override;

  MOCK_METHOD3(registerPrivateKeyMethod,
               void(SSL* ssl, PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher));
  MOCK_METHOD1(unregisterPrivateKeyMethod, void(SSL* ssl));
  MOCK_METHOD0(checkFips, bool());

#ifdef OPENSSL_IS_BORINGSSL
  MOCK_METHOD0(getBoringSslPrivateKeyMethod, BoringSslPrivateKeyMethodSharedPtr());
#endif
};

} // namespace Ssl
} // namespace Envoy
