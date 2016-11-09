#pragma once

#include "envoy/ssl/connection.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats.h"

namespace Ssl {

class MockContextManager : public ContextManager {
public:
  MockContextManager();
  ~MockContextManager();

  MOCK_METHOD3(createSslClientContext,
               Ssl::ClientContext&(const std::string& name, Stats::Store& stats,
                                   ContextConfig& config));
  MOCK_METHOD3(createSslServerContext,
               Ssl::ServerContext&(const std::string& name, Stats::Store& stats,
                                   ContextConfig& config));
  MOCK_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_METHOD0(getContexts, std::vector<std::reference_wrapper<Ssl::Context>>());
};

class MockConnection : public Connection {
public:
  MockConnection();
  ~MockConnection();

  MOCK_METHOD0(sha256PeerCertificateDigest, std::string());
};

class MockClientContext : public ClientContext {
public:
  MockClientContext();
  ~MockClientContext();

  MOCK_METHOD0(daysUntilFirstCertExpires, size_t());
  MOCK_METHOD0(getCaCertInformation, std::string());
  MOCK_METHOD0(getCertChainInformation, std::string());
};

} // Ssl
