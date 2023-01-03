#include "source/extensions/health_checkers/cached/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

SINGLETON_MANAGER_REGISTRATION(cached_health_check_client);

ClientImpl::ClientImpl(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
                       Random::RandomGenerator& random) {
  cache_ = std::make_shared<CacheImpl>(opts, dispatcher, random);
}

void ClientImpl::start(const std::string& hostname) { cache_->add(hostname); }

void ClientImpl::close(const std::string& hostname) { cache_->remove(hostname); }

bool ClientImpl::sendRequest(const std::string& hostname) { return cache_->get(hostname); }

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(Singleton::Manager& singleton_manager,
                                    ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
                                    Random::RandomGenerator& random) {
  auto client = singleton_manager.getTyped<Client>(
      SINGLETON_MANAGER_REGISTERED_NAME(cached_health_check_client),
      [&opts, &dispatcher, &random]() {
        return std::make_shared<ClientImpl>(opts, dispatcher, random);
      });
  return client;
}

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
