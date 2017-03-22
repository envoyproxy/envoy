#pragma once

#include "envoy/redis/codec.h"
#include "envoy/upstream/cluster_manager.h"

namespace Redis {
namespace ConnPool {

/**
 * A handle to an outbound request.
 */
class ActiveRequest {
public:
  virtual ~ActiveRequest() {}

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

/**
 * Outbound request callbacks.
 */
class ActiveRequestCallbacks {
public:
  virtual ~ActiveRequestCallbacks() {}

  /**
   * Called when a pipelined response is received.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(RespValuePtr&& value) PURE;

  /**
   * Called when a network/protocol error occurs and there is no response.
   */
  virtual void onFailure() PURE;
};

/**
 * A single redis client connection.
 */
class Client {
public:
  virtual ~Client() {}

  /**
   * Adds network connection callbacks to the underlying network connection.
   */
  virtual void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) PURE;

  /**
   * Closes the underlying network connection.
   */
  virtual void close() PURE;

  /**
   * Make a pipelined request to the remote redis server.
   * @param request supplies the RESP request to make.
   * @param callbacks supplies the request callbacks.
   * @return ActiveRequest* a handle to the active request.
   */
  virtual ActiveRequest* makeRequest(const RespValue& request,
                                     ActiveRequestCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<Client> ClientPtr;

/**
 * A factory for individual redis client connections.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() {}

  /**
   * Create a client given an upstream host.
   */
  virtual ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher) PURE;
};

/**
 * A redis connection pool. Wraps M connections to N upstream hosts, consistent hashing,
 * pipelining, failure handling, etc.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Makes a redis request.
   * @param hash_key supplies the key to use for consistent hashing.
   * @param request supplies the request to make.
   * @param callbacks supplies the request completion callbacks.
   * @return ActiveRequest* a handle to the active request or nullptr if the request could not
   *         be made for some reason.
   */
  virtual ActiveRequest* makeRequest(const std::string& hash_key, const RespValue& request,
                                     ActiveRequestCallbacks& callbacks) PURE;
};

} // ConnPool
} // Redis
