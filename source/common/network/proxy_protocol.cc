#include "address_impl.h"
#include "listener_impl.h"
#include "proxy_protocol.h"

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/stats/stats.h"

#include "common/common/empty_string.h"
#include "common/network/utility.h"

namespace Network {

const std::string ProxyProtocol::ActiveConnection::PROXY_TCP4 = "PROXY TCP4 ";

ProxyProtocol::ProxyProtocol(Stats::Store& stats_store)
    : stats_{ALL_PROXY_PROTOCOL_STATS(POOL_COUNTER(stats_store))} {}

void ProxyProtocol::newConnection(Event::Dispatcher& dispatcher, int fd, ListenerImpl& listener) {
  std::unique_ptr<ActiveConnection> p{new ActiveConnection(*this, dispatcher, fd, listener)};
  p->moveIntoList(std::move(p), connections_);
}

ProxyProtocol::ActiveConnection::ActiveConnection(ProxyProtocol& parent,
                                                  Event::Dispatcher& dispatcher, int fd,
                                                  ListenerImpl& listener)
    : parent_(parent), fd_(fd), listener_(listener), search_index_(1) {
  file_event_ = dispatcher.createFileEvent(fd, [this](uint32_t events) {
    ASSERT(events == Event::FileReadyType::Read);
    UNREFERENCED_PARAMETER(events);
    onRead();
  }, Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

ProxyProtocol::ActiveConnection::~ActiveConnection() {
  if (fd_ != -1) {
    ::close(fd_);
  }
}

void ProxyProtocol::ActiveConnection::onRead() {
  try {
    onReadWorker();
  } catch (const EnvoyException& ee) {
    parent_.stats_.downstream_cx_proxy_proto_error_.inc();
    close();
  }
}

void ProxyProtocol::ActiveConnection::onReadWorker() {
  std::string proxy_line;
  if (!readLine(fd_, proxy_line)) {
    return;
  }

  if (proxy_line.find(PROXY_TCP4) != 0) {
    throw EnvoyException("failed to read proxy protocol");
  }

  size_t index = proxy_line.find(" ", PROXY_TCP4.size());
  if (index == std::string::npos) {
    throw EnvoyException("failed to read proxy protocol");
  }

  size_t addr_len = index - PROXY_TCP4.size();
  std::string remote_address = proxy_line.substr(PROXY_TCP4.size(), addr_len);

  ListenerImpl& listener = listener_;
  int fd = fd_;
  fd_ = -1;

  removeFromList(parent_.connections_);

  // TODO(mattklein123): Parse the remote port instead of passing zero.
  // TODO(mattklein123): IPv6 support.
  listener.newConnection(
      fd, Network::Address::InstancePtr{new Network::Address::Ipv4Instance(remote_address, 0)},
      listener.socket().localAddress());
}

void ProxyProtocol::ActiveConnection::close() {
  ::close(fd_);
  fd_ = -1;
  removeFromList(parent_.connections_);
}

bool ProxyProtocol::ActiveConnection::readLine(int fd, std::string& s) {
  while (buf_off_ < MAX_PROXY_PROTO_LEN) {
    ssize_t nread = recv(fd, buf_ + buf_off_, MAX_PROXY_PROTO_LEN - buf_off_, MSG_PEEK);

    if (nread == -1 && errno == EAGAIN) {
      return false;
    } else if (nread < 1) {
      throw EnvoyException("failed to read proxy protocol");
    }

    bool found = false;
    // continue searching buf_ from where we left off
    for (; search_index_ < buf_off_ + nread; search_index_++) {
      if (buf_[search_index_] == '\n' && buf_[search_index_ - 1] == '\r') {
        search_index_++;
        found = true;
        break;
      }
    }

    nread = recv(fd, buf_ + buf_off_, search_index_ - buf_off_, 0);

    if (nread < 1) {
      throw EnvoyException("failed to read proxy protocol");
    }

    buf_off_ += nread;

    if (found) {
      s.assign(buf_, buf_off_);
      return true;
    }
  }

  throw EnvoyException("failed to read proxy protocol");
}

} // Network
