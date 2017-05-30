#include "common/network/proxy_protocol.h"

#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/stats/stats.h"

#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

ProxyProtocol::ProxyProtocol(Stats::Scope& scope)
    : stats_{ALL_PROXY_PROTOCOL_STATS(POOL_COUNTER(scope))} {}

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

  // Parse proxy protocol line with format: PROXY TCP4/TCP6 SOURCE_ADDRESS DESTINATION_ADDRESS
  // SOURCE_PORT DESTINATION_PORT.
  auto line_parts = StringUtil::splitKeep(proxy_line, " ", true);

  if (line_parts.size() != 6 || line_parts[0] != "PROXY") {
    throw EnvoyException("failed to read proxy protocol");
  }

  Address::IpVersion protocol_version;
  if (line_parts[1] == "TCP4") {
    protocol_version = Address::IpVersion::v4;
  } else if (line_parts[1] == "TCP6") {
    protocol_version = Address::IpVersion::v6;
  } else {
    throw EnvoyException("failed to read proxy protocol");
  }

  // Error check the source and destination fields. Currently the source port, destination address,
  // and destination port fields are ignored. Remote address refers to the source address.
  Address::InstanceConstSharedPtr remote_address = Utility::parseInternetAddress(line_parts[2]);
  Address::InstanceConstSharedPtr destination_address =
      Utility::parseInternetAddress(line_parts[3]);
  auto remote_version = remote_address->ip()->version();
  auto destination_version = destination_address->ip()->version();
  if (remote_version != protocol_version || destination_version != protocol_version ||
      remote_version != destination_version) {
    throw EnvoyException("failed to read proxy protocol");
  }

  uint64_t remote_port, destination_port;
  try {
    remote_port = std::stol(line_parts[4], nullptr);
    destination_port = std::stol(line_parts[5], nullptr);
  } catch (const std::invalid_argument& ia) {
    throw EnvoyException(ia.what());
  }
  UNREFERENCED_PARAMETER(remote_port);
  UNREFERENCED_PARAMETER(destination_port);

  ListenerImpl& listener = listener_;
  int fd = fd_;
  fd_ = -1;

  removeFromList(parent_.connections_);

  // TODO(mattklein123): Parse the remote port instead of passing zero.
  listener.newConnection(fd, remote_address, listener.socket().localAddress());
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
} // Envoy
