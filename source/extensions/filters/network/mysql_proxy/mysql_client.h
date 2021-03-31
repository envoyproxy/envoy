#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/tcp/conn_pool.h"

#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Outbound request callbacks.
 */
class ClientCallBack {
public:
  virtual ~ClientCallBack() = default;
  /**
   * called when upstream data is received and decoded
   * @param message upstream message, should be always CommandResponse.
   * @param seq seq id of this message
   */
  virtual void onResponse(MySQLCodec& message, uint8_t seq) PURE;
  /**
   * Called when a network/protocol error occurs and there is no response.
   */
  virtual void onFailure() PURE;
};

/**
 * MySQL Client client.
 */
class Client : public Event::DeferredDeletable {
public:
  ~Client() override = default;
  /**
   * Send data to upstream.
   * @param data to be sent.
   */
  virtual void makeRequest(Buffer::Instance& data) PURE;
  /**
   * Closes the underlying network connection.
   */
  virtual void close() PURE;
};

using ClientPtr = std::unique_ptr<Client>;

class ClientFactory {
public:
  virtual ~ClientFactory() = default;
  virtual ClientPtr create(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                           DecoderFactory& decoder_factory, ClientCallBack&) PURE;
};

} // namespace MySQLProxy

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy