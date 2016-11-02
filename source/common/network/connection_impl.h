#pragma once

#include "filter_manager_impl.h"

#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"

namespace Network {

/**
 * Implementation of Network::Connection.
 */
class ConnectionImpl : public virtual Connection,
                       public BufferSource,
                       protected Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd, const std::string& remote_address);
  ~ConnectionImpl();

  // Network::FilterManager
  void addWriteFilter(WriteFilterPtr filter) override;
  void addFilter(FilterPtr filter) override;
  void addReadFilter(ReadFilterPtr filter) override;

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
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
  enum class PostIoAction { Close, KeepOpen };

  virtual void closeSocket();
  void doConnect(const sockaddr* addr, socklen_t addrlen);
  void raiseEvents(uint32_t events);

  FilterManagerImpl filter_manager_;
  const std::string remote_address_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;

private:
  // clang-format off
  struct InternalState {
    static const uint32_t ReadEnabled              = 0x1;
    static const uint32_t Connecting               = 0x2;
    static const uint32_t CloseWithFlush           = 0x4;
    static const uint32_t ImmediateConnectionError = 0x8;
  };
  // clang-format on

  void doLocalClose();
  virtual PostIoAction doReadFromSocket();
  virtual PostIoAction doWriteToSocket();
  void onBufferChange(ConnectionBufferType type, uint64_t old_size, int64_t delta);
  virtual void onConnected();
  void onFileEvent(uint32_t events);
  void onRead();
  void onReadReady();
  void onWriteReady();

  static std::atomic<uint64_t> next_global_id_;

  Event::DispatcherImpl& dispatcher_;
  int fd_{-1};
  Event::FileEventPtr file_event_;
  const uint64_t id_;
  std::list<ConnectionCallbacks*> callbacks_;
  uint32_t state_{InternalState::ReadEnabled};
  Buffer::Instance* current_write_buffer_{};
};

/**
 * libevent implementation of Network::ClientConnection.
 */
class ClientConnectionImpl : public ConnectionImpl, virtual public ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, int fd, const std::string& url);

  static Network::ClientConnectionPtr create(Event::DispatcherImpl& dispatcher,
                                             const std::string& url);
};

class TcpClientConnectionImpl : public ClientConnectionImpl {
public:
  TcpClientConnectionImpl(Event::DispatcherImpl& dispatcher, const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

class UdsClientConnectionImpl final : public ClientConnectionImpl {
public:
  UdsClientConnectionImpl(Event::DispatcherImpl& dispatcher, const std::string& url);

  // Network::ClientConnection
  void connect() override;
};

} // Network
