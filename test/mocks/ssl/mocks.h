#pragma once

#include <functional>
#include <string>

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
  ~MockContextManager();

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
  ~MockConnectionInfo();

  MOCK_CONST_METHOD0(peerCertificatePresented, bool());
  MOCK_CONST_METHOD0(uriSanLocalCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(sha256PeerCertificateDigest, std::string&());
  MOCK_CONST_METHOD0(serialNumberPeerCertificate, std::string());
  MOCK_CONST_METHOD0(subjectPeerCertificate, std::string());
  MOCK_CONST_METHOD0(uriSanPeerCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(subjectLocalCertificate, std::string());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificate, std::string&());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificateChain, std::string&());
  MOCK_CONST_METHOD0(dnsSansPeerCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(dnsSansLocalCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(validFromPeerCertificate, absl::optional<SystemTime>());
  MOCK_CONST_METHOD0(expirationPeerCertificate, absl::optional<SystemTime>());
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext();

  MOCK_CONST_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_CONST_METHOD0(getCaCertInformation, CertificateDetailsPtr());
  MOCK_CONST_METHOD0(getCertChainInformation, std::vector<CertificateDetailsPtr>());
};

class MockPrivateKeyMethodManager : public PrivateKeyMethodManager {
public:
  MockPrivateKeyMethodManager();
  ~MockPrivateKeyMethodManager();

  MOCK_METHOD2(
      createPrivateKeyMethodProvider,
      PrivateKeyMethodProviderSharedPtr(const envoy::api::v2::auth::PrivateKeyMethod& message,
                                        Envoy::Server::Configuration::TransportSocketFactoryContext&
                                            private_key_method_provider_context));
};

class MockPrivateKeyMethodProvider : public PrivateKeyMethodProvider {
public:
  MockPrivateKeyMethodProvider();
  ~MockPrivateKeyMethodProvider();

  MOCK_METHOD3(getPrivateKeyConnection,
               PrivateKeyConnectionPtr(SSL* ssl, PrivateKeyConnectionCallbacks& cb,
                                       Event::Dispatcher& dispatcher));
  MOCK_METHOD0(checkFips, bool());
  MOCK_METHOD0(getBoringSslPrivateKeyMethod, BoringSslPrivateKeyMethodSharedPtr());
};

} // namespace Ssl
} // namespace Envoy
