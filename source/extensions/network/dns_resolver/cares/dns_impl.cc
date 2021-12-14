#include "source/extensions/network/dns_resolver/cares/dns_impl.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/utility.h"

#include "absl/strings/str_join.h"
#include "ares.h"

namespace Envoy {
namespace Network {

DnsResolverImpl::DnsResolverImpl(
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    const envoy::config::core::v3::DnsResolverOptions& dns_resolver_options)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })),
      dns_resolver_options_(dns_resolver_options),
      resolvers_csv_(maybeBuildResolversCsv(resolvers)) {
  AresOptions options = defaultAresOptions();
  initializeChannel(&options.options_, options.optmask_);
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
}

absl::optional<std::string> DnsResolverImpl::maybeBuildResolversCsv(
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) {
  if (resolvers.empty()) {
    return absl::nullopt;
  }

  std::vector<std::string> resolver_addrs;
  resolver_addrs.reserve(resolvers.size());
  for (const auto& resolver : resolvers) {
    // This should be an IP address (i.e. not a pipe).
    if (resolver->ip() == nullptr) {
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
  return {absl::StrJoin(resolver_addrs, ",")};
}

DnsResolverImpl::AresOptions DnsResolverImpl::defaultAresOptions() {
  AresOptions options{};

  if (dns_resolver_options_.use_tcp_for_dns_lookups()) {
    options.optmask_ |= ARES_OPT_FLAGS;
    options.options_.flags |= ARES_FLAG_USEVC;
  }

  if (dns_resolver_options_.no_default_search_domain()) {
    options.optmask_ |= ARES_OPT_FLAGS;
    options.options_.flags |= ARES_FLAG_NOSEARCH;
  }

  return options;
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  dirty_channel_ = false;

  options->sock_state_cb = [](void* arg, os_fd_t fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);

  // Ensure that the channel points to custom resolvers, if they exist.
  if (resolvers_csv_.has_value()) {
    int result = ares_set_servers_ports_csv(channel_, resolvers_csv_->c_str());
    RELEASE_ASSERT(result == ARES_SUCCESS, "");
  }
}

void DnsResolverImpl::AddrInfoPendingResolution::onAresGetAddrInfoCallback(
    int status, int timeouts, ares_addrinfo* addrinfo) {
  if (status != ARES_SUCCESS) {
    ENVOY_LOG_EVENT(debug, "cares_resolution_failure",
                    "dns resolution for {} failed with c-ares status {}", dns_name_, status);
  }

  // We receive ARES_EDESTRUCTION when destructing with pending queries.
  if (status == ARES_EDESTRUCTION) {
    ASSERT(owned_);
    // This destruction might have been triggered by a peer PendingResolution that received a
    // ARES_ECONNREFUSED. If the PendingResolution has not been cancelled that means that the
    // callback_ target _should_ still be around. In that case, raise the callback_ so the target
    // can be done with this query and initiate a new one.
    ENVOY_LOG_EVENT(debug, "cares_dns_resolution_destroyed", "dns resolution for {} destroyed",
                    dns_name_);

    // Nothing can follow a call to finishResolve due to the deletion of this object upon
    // finishResolve().
    finishResolve();
    return;
  }

  if (!dual_resolution_) {
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

  if (status == ARES_SUCCESS) {
    pending_response_.status_ = ResolutionStatus::Success;

    if (addrinfo != nullptr && addrinfo->nodes != nullptr) {
      if (addrinfo->nodes->ai_family == AF_INET) {
        for (const ares_addrinfo_node* ai = addrinfo->nodes; ai != nullptr; ai = ai->ai_next) {
          sockaddr_in address;
          memset(&address, 0, sizeof(address));
          address.sin_family = AF_INET;
          address.sin_port = 0;
          address.sin_addr = reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr;

          pending_response_.address_list_.emplace_back(
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
          pending_response_.address_list_.emplace_back(
              DnsResponse(std::make_shared<const Address::Ipv6Instance>(address),
                          std::chrono::seconds(ai->ai_ttl)));
        }
      }
    }

    if (!pending_response_.address_list_.empty() && dns_lookup_family_ != DnsLookupFamily::All) {
      completed_ = true;
    }

    ASSERT(addrinfo != nullptr);
    ares_freeaddrinfo(addrinfo);
  }

  if (timeouts > 0) {
    ENVOY_LOG(debug, "DNS request timed out {} times", timeouts);
  }

  if (completed_) {
    finishResolve();
    // Nothing can follow a call to finishResolve due to the deletion of this object upon
    // finishResolve().
    return;
  }

  if (dual_resolution_) {
    dual_resolution_ = false;

    // Perform a second lookup for DnsLookupFamily::Auto and DnsLookupFamily::V4Preferred, given
    // that the first lookup failed to return any addresses. Note that DnsLookupFamily::All issues
    // both lookups concurrently so there is no need to fire a second lookup here.
    if (dns_lookup_family_ == DnsLookupFamily::Auto) {
      startResolutionImpl(AF_INET);
    } else if (dns_lookup_family_ == DnsLookupFamily::V4Preferred) {
      startResolutionImpl(AF_INET6);
    }

    // Note: Nothing can follow this call to getAddrInfo due to deletion of this
    // object upon synchronous resolution.
    return;
  }
}

void DnsResolverImpl::PendingResolution::finishResolve() {
  if (!cancelled_) {
    // Use a raw try here because it is used in both main thread and filter.
    // Can not convert to use status code as there may be unexpected exceptions in server fuzz
    // tests, which must be handled. Potential exception may come from getAddressWithPort() or
    // portFromTcpUrl().
    // TODO(chaoqin-li1123): remove try catch pattern here once we figure how to handle unexpected
    // exception in fuzz tests.
    ENVOY_LOG_EVENT(debug, "cares_dns_resolution_complete",
                    "dns resolution for {} completed with status {}", dns_name_,
                    pending_response_.status_);

    TRY_NEEDS_AUDIT {
      callback_(pending_response_.status_, std::move(pending_response_.address_list_));
    }
    catch (const EnvoyException& e) {
      ENVOY_LOG(critical, "EnvoyException in c-ares callback: {}", e.what());
      dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
    }
    catch (const std::exception& e) {
      ENVOY_LOG(critical, "std::exception in c-ares callback: {}", e.what());
      dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
    }
    catch (...) {
      ENVOY_LOG(critical, "Unknown exception in c-ares callback");
      dispatcher_.post([] { throw EnvoyException("unknown"); });
    }
  }
  if (owned_) {
    delete this;
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
  ENVOY_LOG_EVENT(debug, "cares_dns_resolution_start", "dns resolution for {} started", dns_name);

  // TODO(hennna): Add DNS caching which will allow testing the edge case of a
  // failed initial call to getAddrInfo followed by a synchronous IPv4
  // resolution.

  // @see DnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback for why this is done.
  if (dirty_channel_) {
    ares_destroy(channel_);
    AresOptions options = defaultAresOptions();
    initializeChannel(&options.options_, options.optmask_);
  }

  auto pending_resolution = std::make_unique<AddrInfoPendingResolution>(
      *this, callback, dispatcher_, channel_, dns_name, dns_lookup_family);
  pending_resolution->startResolution();

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

DnsResolverImpl::AddrInfoPendingResolution::AddrInfoPendingResolution(
    DnsResolverImpl& parent, ResolveCb callback, Event::Dispatcher& dispatcher,
    ares_channel channel, const std::string& dns_name, DnsLookupFamily dns_lookup_family)
    : PendingResolution(parent, callback, dispatcher, channel, dns_name),
      dns_lookup_family_(dns_lookup_family) {
  if (dns_lookup_family == DnsLookupFamily::Auto ||
      dns_lookup_family == DnsLookupFamily::V4Preferred ||
      dns_lookup_family == DnsLookupFamily::All) {
    dual_resolution_ = true;
  }

  switch (dns_lookup_family_) {
  case DnsLookupFamily::V4Only:
  case DnsLookupFamily::V4Preferred:
    family_ = AF_INET;
    break;
  case DnsLookupFamily::V6Only:
  case DnsLookupFamily::Auto:
    family_ = AF_INET6;
    break;
  // NOTE: DnsLookupFamily::All performs both lookups concurrently as addresses from both families
  // are being requested.
  case DnsLookupFamily::All:
    lookup_all_ = true;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void DnsResolverImpl::AddrInfoPendingResolution::startResolution() {
  if (lookup_all_) {
    startResolutionImpl(AF_INET);
    startResolutionImpl(AF_INET6);
  } else {
    startResolutionImpl(family_);
  }
}

void DnsResolverImpl::AddrInfoPendingResolution::startResolutionImpl(int family) {
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
        static_cast<AddrInfoPendingResolution*>(arg)->onAresGetAddrInfoCallback(status, timeouts,
                                                                                addrinfo);
      },
      this);
}

// c-ares DNS resolver factory
class CaresDnsResolverFactory : public DnsResolverFactory {
public:
  std::string name() const override { return std::string(CaresDnsResolver); }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig()};
  }

  DnsResolverSharedPtr createDnsResolver(Event::Dispatcher& dispatcher, Api::Api&,
                                         const envoy::config::core::v3::TypedExtensionConfig&
                                             typed_dns_resolver_config) const override {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
    std::vector<Network::Address::InstanceConstSharedPtr> resolvers;

    ASSERT(dispatcher.isThreadSafe());
    // Only c-ares DNS factory will call into this function.
    // Directly unpack the typed config to a c-ares object.
    Envoy::MessageUtil::unpackTo(typed_dns_resolver_config.typed_config(), cares);
    dns_resolver_options.MergeFrom(cares.dns_resolver_options());
    if (!cares.resolvers().empty()) {
      const auto& resolver_addrs = cares.resolvers();
      resolvers.reserve(resolver_addrs.size());
      for (const auto& resolver_addr : resolver_addrs) {
        resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
      }
    }
    return std::make_shared<Network::DnsResolverImpl>(dispatcher, resolvers, dns_resolver_options);
  }
};

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(CaresDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
