#include "connection_impl.h"

#include "envoy/event/timer.h"
#include "envoy/common/exception.h"
#include "envoy/network/filter.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"

#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/event.h"

namespace Network {

std::atomic<uint64_t> ConnectionImpl::next_global_id_;
const evbuffer_cb_func ConnectionImpl::read_buffer_cb_ =
    [](evbuffer*, const evbuffer_cb_info* info, void* arg) -> void {
      static_cast<ConnectionImpl*>(arg)->onBufferChange(ConnectionBufferType::Read, info);
    };

const evbuffer_cb_func ConnectionImpl::write_buffer_cb_ =
    [](evbuffer*, const evbuffer_cb_info* info, void* arg) -> void {
      static_cast<ConnectionImpl*>(arg)->onBufferChange(ConnectionBufferType::Write, info);
    };

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher)
    : ConnectionImpl(dispatcher,
                     bufferevent_socket_new(&dispatcher.base(), -1,
                                            BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS),
                     "") {}

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher,
                               Event::Libevent::BufferEventPtr&& bev,
                               const std::string& remote_address)
    : dispatcher_(dispatcher), bev_(std::move(bev)), remote_address_(remote_address),
      id_(++next_global_id_), filter_manager_(*this, *this),
      redispatch_read_event_(dispatcher.createTimer([this]() -> void { onRead(); })),
      read_buffer_(bufferevent_get_input(bev_.get())) {

  enableCallbacks(true, false, true);
  bufferevent_enable(bev_.get(), EV_READ | EV_WRITE);

  evbuffer_add_cb(bufferevent_get_input(bev_.get()), read_buffer_cb_, this);
  evbuffer_add_cb(bufferevent_get_output(bev_.get()), write_buffer_cb_, this);
}

ConnectionImpl::~ConnectionImpl() { ASSERT(!bev_); }

void ConnectionImpl::addWriteFilter(WriteFilterPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ConnectionImpl::addFilter(FilterPtr filter) { filter_manager_.addFilter(filter); }

void ConnectionImpl::addReadFilter(ReadFilterPtr filter) { filter_manager_.addReadFilter(filter); }

void ConnectionImpl::close(ConnectionCloseType type) {
  if (!bev_) {
    return;
  }

  uint64_t data_to_write = evbuffer_get_length(bufferevent_get_output(bev_.get()));
  conn_log_debug("closing data_to_write={}", *this, data_to_write);
  if (data_to_write == 0 || type == ConnectionCloseType::NoFlush) {
    closeNow();
  } else {
    ASSERT(type == ConnectionCloseType::FlushWrite);
    closing_with_flush_ = true;
    enableCallbacks(false, true, true);
  }
}

Connection::State ConnectionImpl::state() {
  if (!bev_) {
    return State::Closed;
  } else if (closing_with_flush_) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ConnectionImpl::closeBev() {
  ASSERT(bev_);
  conn_log_debug("destroying bev", *this);

  // Drain input and output buffers so that callbacks get fired. This does not happen automatically
  // as part of destruction.
  fakeBufferDrain(ConnectionBufferType::Read, bufferevent_get_input(bev_.get()));
  evbuffer_remove_cb(bufferevent_get_input(bev_.get()), read_buffer_cb_, this);

  fakeBufferDrain(ConnectionBufferType::Write, bufferevent_get_output(bev_.get()));
  evbuffer_remove_cb(bufferevent_get_output(bev_.get()), write_buffer_cb_, this);

  bev_.reset();
  redispatch_read_event_->disableTimer();
}

void ConnectionImpl::closeNow() {
  conn_log_debug("closing now", *this);
  closeBev();

  // We expect our owner to deal with freeing us in whatever way makes sense. We raise an event
  // to kick that off.
  raiseEvents(ConnectionEvent::LocalClose);
}

Event::Dispatcher& ConnectionImpl::dispatcher() { return dispatcher_; }

void ConnectionImpl::enableCallbacks(bool read, bool write, bool event) {
  read_enabled_ = false;
  bufferevent_data_cb read_cb = nullptr;
  bufferevent_data_cb write_cb = nullptr;
  bufferevent_event_cb event_cb = nullptr;

  if (read) {
    read_enabled_ = true;
    read_cb = [](bufferevent*, void* ctx) -> void { static_cast<ConnectionImpl*>(ctx)->onRead(); };
  }

  if (write) {
    write_cb =
        [](bufferevent*, void* ctx) -> void { static_cast<ConnectionImpl*>(ctx)->onWrite(); };
  }

  if (event) {
    event_cb = [](bufferevent*, short events, void* ctx)
                   -> void { static_cast<ConnectionImpl*>(ctx)->onEvent(events); };
  }

  bufferevent_setcb(bev_.get(), read_cb, write_cb, event_cb, this);
}

void ConnectionImpl::fakeBufferDrain(ConnectionBufferType type, evbuffer* buffer) {
  if (evbuffer_get_length(buffer) > 0) {
    evbuffer_cb_info info;
    info.n_added = 0;
    info.n_deleted = evbuffer_get_length(buffer);
    info.orig_size = evbuffer_get_length(buffer);

    onBufferChange(type, &info);
  }
}

void ConnectionImpl::noDelay(bool enable) {
  int fd = bufferevent_getfd(bev_.get());

  // There are cases where a connection to localhost can immediately fail (e.g., if the other end
  // does not have enough fds, reaches a backlog limit, etc.). Because we run with deferred error
  // events, the calling code may not yet know that the connection has failed. This is one call
  // where we go outside of libevent and hit the fd directly and this case can fail if the fd is
  // invalid. For this call instead of plumbing through logic that will immediately indicate that a
  // connect failed, we will just ignore the noDelay() call if the socket is invalid since error is
  // going to be raised shortly anyway and it makes the calling code simpler.
  if (fd == -1) {
    return;
  }

  // Don't set NODELAY for unix domain sockets
  sockaddr addr;
  socklen_t len = sizeof(addr);
  int rc = getsockname(fd, &addr, &len);
  RELEASE_ASSERT(rc == 0);

  if (addr.sa_family == AF_UNIX) {
    return;
  }

  // Set NODELAY
  int new_value = enable;
  rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &new_value, sizeof(new_value));
  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);
}

uint64_t ConnectionImpl::id() { return id_; }

void ConnectionImpl::onBufferChange(ConnectionBufferType type, const evbuffer_cb_info* info) {
  // We don't run callbacks deferred so we should only get deleted or added.
  ASSERT(info->n_deleted ^ info->n_added);
  for (ConnectionCallbacks* callbacks : callbacks_) {
    callbacks->onBufferChange(type, info->orig_size, info->n_added - info->n_deleted);
  }
}

void ConnectionImpl::onEvent(short events) {
  uint32_t normalized_events = 0;
  if ((events & BEV_EVENT_EOF) || (events & BEV_EVENT_ERROR)) {
    normalized_events |= ConnectionEvent::RemoteClose;
    closeBev();
  }

  if (events & BEV_EVENT_CONNECTED) {
    normalized_events |= ConnectionEvent::Connected;
  }

  ASSERT(normalized_events != 0);
  conn_log_debug("event: {}", *this, normalized_events);
  raiseEvents(normalized_events);
}

void ConnectionImpl::onRead() {
  // Cancel the redispatch event in case we raced with a network event.
  redispatch_read_event_->disableTimer();
  if (read_buffer_.length() == 0) {
    return;
  }

  filter_manager_.onRead();
}

void ConnectionImpl::onWrite() {
  conn_log_debug("write flush complete", *this);
  closeNow();
}

void ConnectionImpl::readDisable(bool disable) {
  bool read_enabled = readEnabled();
  UNREFERENCED_PARAMETER(read_enabled);
  conn_log_trace("readDisable: enabled={} disable={}", *this, read_enabled, disable);

  // We do not actually disable reading from the socket. We just stop firing read callbacks.
  // This allows us to still detect remote close in a timely manner. In practice there is a chance
  // that a bad client could send us a large amount of data on a HTTP/1.1 connection while we are
  // processing the current request.
  // TODO: Add buffered data stats and potentially fail safe processing that disconnects or
  //       applies back pressure to bad HTTP/1.1 clients.
  if (disable) {
    ASSERT(read_enabled);
    enableCallbacks(false, false, true);
  } else {
    ASSERT(!read_enabled);
    enableCallbacks(true, false, true);
    redispatch_read_event_->enableTimer(std::chrono::milliseconds::zero());
  }
}

void ConnectionImpl::raiseEvents(uint32_t events) {
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onEvent(events);
  }
}

bool ConnectionImpl::readEnabled() { return read_enabled_; }

void ConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) { callbacks_.push_back(&cb); }

void ConnectionImpl::write(Buffer::Instance& data) {
  // NOTE: This is kind of a hack, but currently we don't support restart/continue on the write
  //       path, so we just pass around the buffer passed to us in this function. If we ever support
  //       buffer/restart/continue on the write path this needs to get more complicated.
  current_write_buffer_ = &data;
  FilterStatus status = filter_manager_.onWrite();
  current_write_buffer_ = nullptr;

  if (FilterStatus::StopIteration == status) {
    return;
  }

  if (data.length() > 0) {
    conn_log_trace("writing {} bytes", *this, data.length());
    for (const Buffer::RawSlice& slice : data.getRawSlices()) {
      int rc = bufferevent_write(bev_.get(), slice.mem_, slice.len_);
      ASSERT(rc == 0);
      UNREFERENCED_PARAMETER(rc);
    }
  }
}

ClientConnectionImpl::ClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                           Event::Libevent::BufferEventPtr&& bev,
                                           const std::string& url)
    : ConnectionImpl(dispatcher, std::move(bev), url) {}

Network::ClientConnectionPtr ClientConnectionImpl::create(Event::DispatcherImpl& dispatcher,
                                                          Event::Libevent::BufferEventPtr&& bev,
                                                          const std::string& url) {
  if (url.find(Network::Utility::TCP_SCHEME) == 0) {
    return Network::ClientConnectionPtr{
        new Network::TcpClientConnectionImpl(dispatcher, std::move(bev), url)};
  } else if (url.find(Network::Utility::UNIX_SCHEME) == 0) {
    return Network::ClientConnectionPtr{
        new Network::UdsClientConnectionImpl(dispatcher, std::move(bev), url)};
  } else {
    throw EnvoyException(fmt::format("malformed url: {}", url));
  }
}

TcpClientConnectionImpl::TcpClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                                 Event::Libevent::BufferEventPtr&& bev,
                                                 const std::string& url)
    : ClientConnectionImpl(dispatcher, std::move(bev), url) {}

void TcpClientConnectionImpl::connect() {
  AddrInfoPtr addr_info = Utility::resolveTCP(Utility::hostFromUrl(remote_address_),
                                              Utility::portFromUrl(remote_address_));
  bufferevent_socket_connect(bev_.get(), addr_info->ai_addr, addr_info->ai_addrlen);
}

UdsClientConnectionImpl::UdsClientConnectionImpl(Event::DispatcherImpl& dispatcher,
                                                 Event::Libevent::BufferEventPtr&& bev,
                                                 const std::string& url)
    : ClientConnectionImpl(dispatcher, std::move(bev), url) {}

void UdsClientConnectionImpl::connect() {
  sockaddr_un addr = Utility::resolveUnixDomainSocket(Utility::pathFromUrl(remote_address_));
  bufferevent_socket_connect(bev_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_un));
}

} // Network
