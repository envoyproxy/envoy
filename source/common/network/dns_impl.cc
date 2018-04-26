#include "common/network/dns_impl.h"

#include <netdb.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "ares.h"

namespace Envoy {
namespace Network {

DnsResolverImpl::DnsResolverImpl(
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })) {
  // This is also done in main(), to satisfy the requirement that c-ares is
  // initialized prior to threading. The additional call to ares_library_init()
  // here is a nop in normal execution, but exists for testing where we don't
  // launch via main().
  ares_library_init(ARES_LIB_INIT_ALL);
  ares_options options;

  initializeChannel(&options, 0);

  if (!resolvers.empty()) {
    std::vector<std::string> resolver_addrs;
    resolver_addrs.reserve(resolvers.size());
    for (const auto& resolver : resolvers) {
      // This should be an IP address (i.e. not a pipe).
      if (resolver->ip() == nullptr) {
        ares_destroy(channel_);
        ares_library_cleanup();
        throw EnvoyException(
            fmt::format("DNS resolver '{}' is not an IP address", resolver->asString()));
      }
      resolver_addrs.push_back(resolver->asString());
    }
    const std::string resolvers_csv = StringUtil::join(resolver_addrs, ",");
    int result = ares_set_servers_ports_csv(channel_, resolvers_csv.c_str());
    RELEASE_ASSERT(result == ARES_SUCCESS);
  }
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
  ares_library_cleanup();
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  options->sock_state_cb = [](void* arg, int fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);
}

void DnsResolverImpl::PendingResolution::onAresHostCallback(int status, int timeouts,
                                                            hostent* hostent) {
  // We receive ARES_EDESTRUCTION when destructing with pending queries.
  if (status == ARES_EDESTRUCTION) {
    ASSERT(owned_);
    delete this;
    return;
  }
  if (!fallback_if_failed_) {
    completed_ = true;
  }

  std::list<Address::InstanceConstSharedPtr> address_list;
  if (status == ARES_SUCCESS) {
    if (hostent->h_addrtype == AF_INET) {
      for (int i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
        ASSERT(hostent->h_length == sizeof(in_addr));
        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr = *reinterpret_cast<in_addr*>(hostent->h_addr_list[i]);
        address_list.emplace_back(new Address::Ipv4Instance(&address));
      }
    } else if (hostent->h_addrtype == AF_INET6) {
      for (int i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
        ASSERT(hostent->h_length == sizeof(in6_addr));
        sockaddr_in6 address;
        memset(&address, 0, sizeof(address));
        address.sin6_family = AF_INET6;
        address.sin6_port = 0;
        address.sin6_addr = *reinterpret_cast<in6_addr*>(hostent->h_addr_list[i]);
        address_list.emplace_back(new Address::Ipv6Instance(address));
      }
    }
    if (!address_list.empty()) {
      completed_ = true;
    }
  }

  if (timeouts > 0) {
    ENVOY_LOG(debug, "DNS request timed out {} times", timeouts);
  }

  if (completed_) {
    if (!cancelled_) {
      callback_(std::move(address_list));
    }
    if (owned_) {
      delete this;
      return;
    }
  }

  if (!completed_ && fallback_if_failed_) {
    fallback_if_failed_ = false;
    getHostByName(AF_INET);
    // Note: Nothing can follow this call to getHostByName due to deletion of this
    // object upon synchronous resolution.
    return;
  }
}

void DnsResolverImpl::updateAresTimer() {
  // Update the timeout for events.
  timeval timeout;
  timeval* timeout_result = ares_timeout(channel_, nullptr, &timeout);
  if (timeout_result != nullptr) {
    const auto ms =
        std::chrono::milliseconds(timeout_result->tv_sec * 1000 + timeout_result->tv_usec / 1000);
    ENVOY_LOG(debug, "Setting DNS resolution timer for {} milliseconds", ms.count());
    timer_->enableTimer(ms);
  } else {
    timer_->disableTimer();
  }
}

void DnsResolverImpl::onEventCallback(int fd, uint32_t events) {
  const ares_socket_t read_fd = events & Event::FileReadyType::Read ? fd : ARES_SOCKET_BAD;
  const ares_socket_t write_fd = events & Event::FileReadyType::Write ? fd : ARES_SOCKET_BAD;
  ares_process_fd(channel_, read_fd, write_fd);
  updateAresTimer();
}

void DnsResolverImpl::onAresSocketStateChange(int fd, int read, int write) {
  updateAresTimer();
  auto it = events_.find(fd);
  // Stop tracking events for fd if no more state change events.
  if (read == 0 && write == 0) {
    if (it != events_.end()) {
      events_.erase(it);
    }
    return;
  }

  // If we weren't tracking the fd before, create a new FileEvent.
  if (it == events_.end()) {
    events_[fd] = dispatcher_.createFileEvent(
        fd, [this, fd](uint32_t events) { onEventCallback(fd, events); },
        Event::FileTriggerType::Level, Event::FileReadyType::Read | Event::FileReadyType::Write);
  }
  events_[fd]->setEnabled((read ? Event::FileReadyType::Read : 0) |
                          (write ? Event::FileReadyType::Write : 0));
}

ActiveDnsQuery* DnsResolverImpl::resolve(const std::string& dns_name,
                                         DnsLookupFamily dns_lookup_family, ResolveCb callback) {
  // TODO(hennna): Add DNS caching which will allow testing the edge case of a
  // failed intial call to getHostbyName followed by a synchronous IPv4
  // resolution.
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(callback, channel_, dns_name));
  if (dns_lookup_family == DnsLookupFamily::Auto) {
    pending_resolution->fallback_if_failed_ = true;
  }

  if (dns_lookup_family == DnsLookupFamily::V4Only) {
    pending_resolution->getHostByName(AF_INET);
  } else {
    pending_resolution->getHostByName(AF_INET6);
  }

  if (pending_resolution->completed_) {
    // Resolution does not need asynchronous behavior or network events. For
    // example, localhost lookup.
    return nullptr;
  } else {
    // Enable timer to wake us up if the request times out.
    updateAresTimer();

    // The PendingResolution will self-delete when the request completes
    // (including if cancelled or if ~DnsResolverImpl() happens).
    pending_resolution->owned_ = true;
    return pending_resolution.release();
  }
}

void DnsResolverImpl::PendingResolution::getHostByName(int family) {
  ares_gethostbyname(channel_, dns_name_.c_str(), family,
                     [](void* arg, int status, int timeouts, hostent* hostent) {
                       static_cast<PendingResolution*>(arg)->onAresHostCallback(status, timeouts,
                                                                                hostent);
                     },
                     this);
}

} // namespace Network
} // namespace Envoy
