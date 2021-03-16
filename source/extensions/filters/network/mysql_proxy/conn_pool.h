#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnectionPool {
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
 * Cancellable used for callee to cancel the client pool request.
 */
class Cancellable {
public:
  virtual ~Cancellable() = default;
  /**
   * Cancel the client pool request.
   */
  virtual void cancel() PURE;
};

/**
 * ClientData is a wrapper of connection to MySQL server.
 */
class ClientData {
public:
  virtual ~ClientData() = default;
  /**
   * Reset the decoder of client
   * @param decoder the decoder which we want client to use.
   */
  virtual void resetClient(DecoderPtr&& decoder) PURE;
  /**
   * Send data to MySQL server
   * @param data data to be sent.
   */
  virtual void sendData(Buffer::Instance& data) PURE;
  /**
   * Decoder getter of client data
   * @return Decoder& decoder of client data
   */
  virtual Decoder& decoder() PURE;
  /**
   * Closer of client data, in most cases, close() will put the client into the client pool.
   */
  virtual void close() PURE;
};

using ClientDataPtr = std::unique_ptr<ClientData>;

/**
 * MySQL Client Pool call back
 */
class ClientPoolCallBack {
public:
  virtual ~ClientPoolCallBack() = default;
  /**
   * onClientReady called when connection is ready and pass the MySQL connection phase
   * @param client_data Client Data of ready connection
   */
  virtual void onClientReady(ClientDataPtr&& client_data) PURE;
  /**
   * onClientFailure called when proxy failed to get connection of upstream or failed to pass
   * connection phase.
   * @param reason reason of failure
   */
  virtual void onClientFailure(MySQLPoolFailureReason reason) PURE;
};

/*
 * MySQL Connection pool of upstream
 */
class Instance {
public:
  virtual ~Instance() = default;
  /**
   * Create a new client on the pool.
   * @param cb supplies the callbacks to invoke when the client is ready or has failed. The
   *           callbacks may be invoked immediately within the context of this call if there is a
   *           ready client or an immediate failure. In this case, the routine returns nullptr.
   * @return Cancellable* If no client is ready, the callback is not invoked, and a handle
   *                      is returned that can be used to cancel the request. Otherwise, one of the
   *                      callbacks is called and the routine returns nullptr. NOTE: Once a callback
   *                      is called, the handle is no longer valid and any further cancellation
   *                      should be done by resetting the client.
   */
  virtual Cancellable* newMySQLClient(ClientPoolCallBack& cb) PURE;
};

using ClientPoolSharedPtr = std::shared_ptr<Instance>;

class InstanceFactory {
public:
  virtual ~InstanceFactory() = default;
  virtual ClientPoolSharedPtr
  create(ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::
             ConnectionPoolSettings& setting,
         DecoderFactory& decoder_factory, const std::string& auth_username,
         const std::string& auth_password) PURE;
};

} // namespace ConnectionPool

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
