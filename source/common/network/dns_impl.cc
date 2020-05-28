#include "common/network/dns_impl.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "absl/strings/str_join.h"
#include "ares.h"

namespace Envoy {
namespace Network {

DnsResolverImpl::DnsResolverImpl(
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    const bool use_tcp_for_dns_lookups)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })),
      use_tcp_for_dns_lookups_(use_tcp_for_dns_lookups) {

  AresOptions options = defaultAresOptions();
  initializeChannel(&options.options_, options.optmask_);

  if (!resolvers.empty()) {
    std::vector<std::string> resolver_addrs;
    resolver_addrs.reserve(resolvers.size());
    for (const auto& resolver : resolvers) {
      // This should be an IP address (i.e. not a pipe).
      if (resolver->ip() == nullptr) {
        ares_destroy(channel_);
        throw EnvoyException(
            fmt::format("DNS resolver '{}' is not an IP address", resolver->asString()));
      }
      // Note that the ip()->port() may be zero if the port is not fully specified by the
      // Address::Instance.
      // resolver->asString() is avoided as that format may be modified by custom
      // Address::Instance implementations in ways that make the <port> not a simple
      // integer. See https://github.com/envoyproxy/envoy/pull/3366.
      resolver_addrs.push_back(fmt::format(resolver->ip()->ipv6() ? "[{}]:{}" : "{}:{}",
                                           resolver->ip()->addressAsString(),
                                           resolver->ip()->port()));
    }
    const std::string resolvers_csv = absl::StrJoin(resolver_addrs, ",");
    int result = ares_set_servers_ports_csv(channel_, resolvers_csv.c_str());
    RELEASE_ASSERT(result == ARES_SUCCESS, "");
  }
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
}

DnsResolverImpl::AresOptions DnsResolverImpl::defaultAresOptions() {
  AresOptions options{};

  if (use_tcp_for_dns_lookups_) {
    options.optmask_ |= ARES_OPT_FLAGS;
    options.options_.flags |= ARES_FLAG_USEVC;
  }

  return options;
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  options->sock_state_cb = [](void* arg, os_fd_t fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);
}

void DnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback(int status, int timeouts,
                                                                   ares_addrinfo* addrinfo) {
  // We receive ARES_EDESTRUCTION when destructing with pending queries.
  if (status == ARES_EDESTRUCTION) {
    ASSERT(owned_);
    // This destruction might have been triggered by a peer PendingResolution that received a
    // ARES_ECONNREFUSED. If the PendingResolution has not been cancelled that means that the
    // callback_ target _should_ still be around. In that case, raise the callback_ so the target
    // can be done with this query and initiate a new one.
    if (!cancelled_) {
      callback_(ResolutionStatus::Failure, {});
    }
    delete this;
    return;
  }
  if (!fallback_if_failed_) {
    completed_ = true;

    // If c-ares returns ARES_ECONNREFUSED and there is no fallback we assume that the channel_ is
    // broken. Mark the channel dirty so that it is destroyed and reinitialized on a subsequent call
    // to DnsResolver::resolve(). The optimal solution would be for c-ares to reinitialize the
    // channel, and not have Envoy track side effects.
    // context: https://github.com/envoyproxy/envoy/issues/4543 and
    // https://github.com/c-ares/c-ares/issues/301.
    //
    // The channel cannot be destroyed and reinitialized here because that leads to a c-ares
    // segfault.
    if (status == ARES_ECONNREFUSED) {
      parent_.dirty_channel_ = true;
    }
  }

  std::list<DnsResponse> address_list;
  ResolutionStatus resolution_status;
  if (status == ARES_SUCCESS) {
    resolution_status = ResolutionStatus::Success;
    if (addrinfo != nullptr && addrinfo->nodes != nullptr) {
      if (addrinfo->nodes->ai_family == AF_INET) {
        for (const ares_addrinfo_node* ai = addrinfo->nodes; ai != nullptr; ai = ai->ai_next) {
          sockaddr_in address;
          memset(&address, 0, sizeof(address));
          address.sin_family = AF_INET;
          address.sin_port = 0;
          address.sin_addr = reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr;

          address_list.emplace_back(
              DnsResponse(std::make_shared<const Address::Ipv4Instance>(&address),
                          std::chrono::seconds(ai->ai_ttl)));
        }
      } else if (addrinfo->nodes->ai_family == AF_INET6) {
        for (const ares_addrinfo_node* ai = addrinfo->nodes; ai != nullptr; ai = ai->ai_next) {
          sockaddr_in6 address;
          memset(&address, 0, sizeof(address));
          address.sin6_family = AF_INET6;
          address.sin6_port = 0;
          address.sin6_addr = reinterpret_cast<sockaddr_in6*>(ai->ai_addr)->sin6_addr;
          address_list.emplace_back(
              DnsResponse(std::make_shared<const Address::Ipv6Instance>(address),
                          std::chrono::seconds(ai->ai_ttl)));
        }
      }
    }

    if (!address_list.empty()) {
      completed_ = true;
    }

    ASSERT(addrinfo != nullptr);
    ares_freeaddrinfo(addrinfo);
  } else {
    resolution_status = ResolutionStatus::Failure;
  }

  if (timeouts > 0) {
    ENVOY_LOG(debug, "DNS request timed out {} times", timeouts);
  }

  if (completed_) {
    if (!cancelled_) {
      try {
        callback_(resolution_status, std::move(address_list));
      } catch (const EnvoyException& e) {
        ENVOY_LOG(critical, "EnvoyException in c-ares callback: {}", e.what());
        dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
      } catch (const std::exception& e) {
        ENVOY_LOG(critical, "std::exception in c-ares callback: {}", e.what());
        dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
      } catch (...) {
        ENVOY_LOG(critical, "Unknown exception in c-ares callback");
        dispatcher_.post([] { throw EnvoyException("unknown"); });
      }
    }
    if (owned_) {
      delete this;
      return;
    }
  }

  if (!completed_ && fallback_if_failed_) {
    fallback_if_failed_ = false;
    getAddrInfo(AF_INET);
    // Note: Nothing can follow this call to getAddrInfo due to deletion of this
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
    ENVOY_LOG(trace, "Setting DNS resolution timer for {} milliseconds", ms.count());
    timer_->enableTimer(ms);
  } else {
    timer_->disableTimer();
  }
}

void DnsResolverImpl::onEventCallback(os_fd_t fd, uint32_t events) {
  const ares_socket_t read_fd = events & Event::FileReadyType::Read ? fd : ARES_SOCKET_BAD;
  const ares_socket_t write_fd = events & Event::FileReadyType::Write ? fd : ARES_SOCKET_BAD;
  ares_process_fd(channel_, read_fd, write_fd);
  updateAresTimer();
}

void DnsResolverImpl::onAresSocketStateChange(os_fd_t fd, int read, int write) {
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
  // failed initial call to getAddrInfo followed by a synchronous IPv4
  // resolution.

  // @see DnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback for why this is done.
  if (dirty_channel_) {
    dirty_channel_ = false;
    ares_destroy(channel_);

    AresOptions options = defaultAresOptions();
    initializeChannel(&options.options_, options.optmask_);
  }
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(*this, callback, dispatcher_, channel_, dns_name));
  if (dns_lookup_family == DnsLookupFamily::Auto) {
    pending_resolution->fallback_if_failed_ = true;
  }

  if (dns_lookup_family == DnsLookupFamily::V4Only) {
    pending_resolution->getAddrInfo(AF_INET);
  } else {
    pending_resolution->getAddrInfo(AF_INET6);
  }

  if (pending_resolution->completed_) {
    // Resolution does not need asynchronous behavior or network events. For
    // example, localhost lookup.
    return nullptr;
  } else {
    // Enable timer to wake us up if the request times out.
    updateAresTimer();

    // The PendingResolution will self-delete when the request completes (including if cancelled or
    // if ~DnsResolverImpl() happens via ares_destroy() and subsequent handling of ARES_EDESTRUCTION
    // in DnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback()).
    pending_resolution->owned_ = true;
    return pending_resolution.release();
  }
}

void DnsResolverImpl::PendingResolution::getAddrInfo(int family) {
  struct ares_addrinfo_hints hints = {};
  hints.ai_family = family;

  /**
   * ARES_AI_NOSORT result addresses will not be sorted and no connections to resolved addresses
   * will be attempted
   */
  hints.ai_flags = ARES_AI_NOSORT;

  ares_getaddrinfo(
      channel_, dns_name_.c_str(), /* service */ nullptr, &hints,
      [](void* arg, int status, int timeouts, ares_addrinfo* addrinfo) {
        static_cast<PendingResolution*>(arg)->onAresGetAddrInfoCallback(status, timeouts, addrinfo);
      },
      this);
}

} // namespace Network
} // namespace Envoy
