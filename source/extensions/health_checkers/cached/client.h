#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

/**
 * A singleton cached client connection.
 */
class Client {
public:
  virtual ~Client() = default;

  /**
   * Initialize the connection.
   */
  virtual void start(const std::string& hostname) PURE;

  /**
   * Send the health check request.
   */
  virtual bool sendRequest(const std::string& hostname) PURE;

  /**
   * Close the underlying network connection.
   */
  virtual void close(const std::string& hostname) PURE;
};

using ClientPtr = std::shared_ptr<Client>;

struct TlsOptions {
  bool enabled = false;

  std::string cacert;

  std::string capath;

  std::string cert;

  std::string key;

  std::string sni;
};

struct ConnectionOptions {
  std::string host;

  int port = 6379;

  std::string user = "default";

  std::string password;

  uint32_t db = 0;

  std::chrono::milliseconds connect_timeout{0};

  std::chrono::milliseconds command_timeout{0};

  TlsOptions tls;
};

using ConnectionOptionsPtr = std::shared_ptr<ConnectionOptions>;

/**
 * A factory for the singleton cached client connection.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() = default;

  /**
   * Create a singleton cached client connection.
   * @param the server-wide singleton manager.
   * @param the connection options to underlying network connection.
   * @param the main thread's dispatcher.
   * @param the random number generator.
   *         for all singleton processing.
   */
  virtual ClientPtr create(Singleton::Manager& singleton_manager, ConnectionOptionsPtr opts,
                           Event::Dispatcher& dispatcher, Random::RandomGenerator& random) PURE;
};

/**
 * A cache for host health check status.
 */

class Cache {
public:
  virtual ~Cache() = default;

  /**
   * Add host health check status entry in the cache.
   */
  virtual void add(const std::string& hostname) PURE;

  /**
   * Remove host health check status entry in the cache.
   */
  virtual void remove(const std::string& hostname) PURE;

  /**
   * Get host health check status entry in the cache.
   */
  virtual bool get(const std::string& hostname) PURE;

  /**
   * Callback for remote cache set health check status.
   */
  virtual void onSetCache(const std::string& hostname) PURE;

  /**
   * Callback for get health check status from remote cache.
   */
  virtual void onGetCache(const std::string& hostname, const std::string& status) PURE;
};

using CachePtr = std::shared_ptr<Cache>;

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
