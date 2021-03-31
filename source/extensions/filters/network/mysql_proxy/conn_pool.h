#pragma once
#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/conn_pool/conn_pool_base.h"

#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {
using PoolFailureReason = Tcp::ConnectionPool::PoolFailureReason;

enum class MySQLPoolFailureReason {
  // A resource overflowed and policy prevented a new connection from being created.
  Overflow = static_cast<int>(PoolFailureReason::Overflow),
  // A local connection failure took place while creating a new connection.
  LocalConnectionFailure = static_cast<int>(PoolFailureReason::LocalConnectionFailure),
  // A remote connection failure took place while creating a new connection.
  RemoteConnectionFailure = static_cast<int>(PoolFailureReason::RemoteConnectionFailure),
  // A timeout occurred while creating a new connection.
  Timeout = static_cast<int>(PoolFailureReason::Timeout),
  // A auth failure when connect to upstream
  AuthFailure,
  // A parse error when parse upstream data
  ParseFailure,
};
/**
 * MySQL Client Pool call back
 */
class ClientPoolCallBack {
public:
  virtual ~ClientPoolCallBack() = default;
  /**
   * Called when a pool error occurred and no connection could be acquired for making the request.
   * @param reason supplies the failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onPoolFailure(MySQLPoolFailureReason reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Called when a connection is available to process a request/response. Connections may be
   * released back to the pool for re-use by resetting the ConnectionDataPtr. If the connection is
   * no longer viable for reuse (e.g. due to some kind of protocol error), the underlying
   * ClientConnection should be closed to prevent its reuse.
   *
   * @param conn supplies the connection data to use.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   */
  virtual void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                           Upstream::HostDescriptionConstSharedPtr host) PURE;
};

class Instance : public Envoy::ConnectionPool::Instance, public Event::DeferredDeletable {
public:
  ~Instance() override = default;
  virtual Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callback) PURE;
  virtual void closeConnections() PURE;
};

/*
 * cluster connection pool manager, per connection pool per host.
 */
class ConnectionPoolManager {
public:
  virtual ~ConnectionPoolManager() = default;
  // now use defualt lb to choose host.
  virtual Tcp::ConnectionPool::Cancellable* newConnection(ClientPoolCallBack& callbacks) PURE;
};

using InstancePtr = std::unique_ptr<Instance>;
using ConnectionPoolManagerSharedPtr = std::shared_ptr<ConnectionPoolManager>;

class ConnectionPoolManagerFactory {
public:
  virtual ~ConnectionPoolManagerFactory() = default;
  // now use defualt lb to choose host.
  virtual ConnectionPoolManagerSharedPtr
  create(Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls, Api::Api& api,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         DecoderFactory& decoder_factory) PURE;
};

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy