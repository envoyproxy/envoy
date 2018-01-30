#pragma once

#include <functional>
#include <string>

#include "envoy/ssl/connection.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Ssl {

class MockContextManager : public ContextManager {
public:
  MockContextManager();
  ~MockContextManager();

  ClientContextPtr createSslClientContext(Stats::Scope& scope,
                                          const ClientContextConfig& config) override {
    return ClientContextPtr{createSslClientContext_(scope, config)};
  }

  ServerContextPtr createSslServerContext(const std::string& listener_name,
                                          const std::vector<std::string>& server_names,
                                          Stats::Scope& scope, const ServerContextConfig& config,
                                          bool skip_context_update) override {
    return ServerContextPtr{
        createSslServerContext_(listener_name, server_names, scope, config, skip_context_update)};
  }

  MOCK_METHOD2(createSslClientContext_,
               ClientContext*(Stats::Scope& scope, const ClientContextConfig& config));
  MOCK_METHOD5(createSslServerContext_,
               ServerContext*(const std::string& listener_name,
                              const std::vector<std::string>& server_names, Stats::Scope& stats,
                              const ServerContextConfig& config, bool skip_context_update));
  MOCK_CONST_METHOD2(findSslServerContext, ServerContext*(const std::string&, const std::string&));
  MOCK_CONST_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_METHOD1(iterateContexts, void(std::function<void(const Context&)> callback));
};

class MockConnection : public Connection {
public:
  MockConnection();
  ~MockConnection();

  MOCK_CONST_METHOD0(peerCertificatePresented, bool());
  MOCK_METHOD0(uriSanLocalCertificate, std::string());
  MOCK_METHOD0(sha256PeerCertificateDigest, std::string());
  MOCK_CONST_METHOD0(subjectPeerCertificate, std::string());
  MOCK_METHOD0(uriSanPeerCertificate, std::string());
  MOCK_CONST_METHOD0(subjectLocalCertificate, std::string());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificate, std::string());
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext();

  MOCK_CONST_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_CONST_METHOD0(getCaCertInformation, std::string());
  MOCK_CONST_METHOD0(getCertChainInformation, std::string());
};

} // namespace Ssl
} // namespace Envoy
