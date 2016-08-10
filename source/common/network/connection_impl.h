#pragma once

#include "filter_manager.h"

#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"

// Forward decls to avoid leaking libevent headers to reset of program.
struct evbuffer_cb_info;
typedef void (*evbuffer_cb_func)(evbuffer* buffer, const evbuffer_cb_info* info, void* arg);

namespace Network {

/**
 * libevent implementation of Network::Connection.
 */
class ConnectionImpl : public virtual Connection,
                       public BufferSource,
                       protected Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionImpl(Event::DispatcherImpl& dispatcher);
  ConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                 const std::string& remote_address);
  ~ConnectionImpl();

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void addWriteFilter(WriteFilterPtr filter) override;
  void addFilter(FilterPtr filter) override;
  void addReadFilter(ReadFilterPtr filter) override;
  void close(ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override;
  uint64_t id() override;
  std::string nextProtocol() override { return ""; }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  bool readEnabled() override;
  const std::string& remoteAddress() override { return remote_address_; }
  Ssl::Connection* ssl() override { return nullptr; }
  State state() override;
  void write(Buffer::Instance& data) override;

  // Network::BufferSource
  Buffer::Instance& getReadBuffer() override { return read_buffer_; }
  Buffer::Instance& getWriteBuffer() override { return *current_write_buffer_; }

protected:
  /**
   * Called via close() when there is no data to flush or when data has been flushed.
   */
  void closeNow();

  /**
   * Enable or disable each of the 3 socket level callbacks.
   */
  void enableCallbacks(bool read, bool write, bool event);

  /**
   * Called by libevent when an owned evbuffer changes size.
   */
  void onBufferChange(ConnectionBufferType type, const evbuffer_cb_info* info);

  /**
   * Fires when a connection event occurs.
   * @param events supplies the libevent events that fired.
   */
  virtual void onEvent(short events);

  /**
   * Fires when new data is available on the connection.
   */
  void onRead();

  /**
   * Fires when the write buffer empties. This is only used when doing a flush/close operation.
   */
  void onWrite();

  /**
   * Send connection events to all registered callbacks.
   */
  void raiseEvents(uint32_t events);

  /**
   * Close the underlying socket connection
   */
  virtual void closeBev();

  static std::atomic<uint64_t> next_global_id_;
  static const evbuffer_cb_func read_buffer_cb_;
  static const evbuffer_cb_func write_buffer_cb_;

  Event::DispatcherImpl& dispatcher_;
  Event::Libevent::BufferEventPtr bev_;
  const std::string remote_address_;
  const uint64_t id_;
  FilterManager filter_manager_;
  std::list<ConnectionCallbacks*> callbacks_;
  Event::TimerPtr redispatch_read_event_;
  bool read_enabled_;
  bool closing_with_flush_{};

private:
  void fakeBufferDrain(ConnectionBufferType type, evbuffer* buffer);

  Buffer::WrappedImpl read_buffer_;
  Buffer::Instance* current_write_buffer_{};
};

/**
 * libevent implementation of Network::ClientConnection.
 */
class ClientConnectionImpl : public ConnectionImpl, virtual public ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                       const std::string& url);

  static Network::ClientConnectionPtr create(Event::DispatcherImpl& dispatcher,
                                             Event::Libevent::BufferEventPtr&& bev,
                                             const std::string& url);
};

class TcpClientConnectionImpl : public ClientConnectionImpl {
public:
  TcpClientConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                          const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

class UdsClientConnectionImpl final : public ClientConnectionImpl {
public:
  UdsClientConnectionImpl(Event::DispatcherImpl& dispatcher, Event::Libevent::BufferEventPtr&& bev,
                          const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

} // Network
