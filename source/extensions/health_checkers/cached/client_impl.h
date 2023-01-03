#pragma once

#include "source/extensions/health_checkers/cached/client.h"
#include "source/extensions/health_checkers/cached/hiredis.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

class ClientImpl : public Client, public Singleton::Instance {
public:
  ClientImpl(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
             Random::RandomGenerator& random);

  void start(const std::string& hostname) override;
  bool sendRequest(const std::string& hostname) override;
  void close(const std::string& hostname) override;

private:
  CachePtr cache_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  // ClientFactory
  ClientPtr create(Singleton::Manager& singleton_manager, ConnectionOptionsPtr opts,
                   Event::Dispatcher& dispatcher, Random::RandomGenerator& random) override;

  static ClientFactoryImpl instance_;
};

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
