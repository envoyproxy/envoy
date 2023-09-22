#include "source/extensions/network/dns_resolver/cares/dns_impl.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/registry/registry.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_join.h"
#include "ares.h"

namespace Envoy {
namespace Network {

DnsResolverImpl::DnsResolverImpl(
    const envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig& config,
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    Stats::Scope& root_scope)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })),
      dns_resolver_options_(config.dns_resolver_options()),
      use_resolvers_as_fallback_(config.use_resolvers_as_fallback()),
      resolvers_csv_(maybeBuildResolversCsv(resolvers)),
      filter_unroutable_families_(config.filter_unroutable_families()),
      scope_(root_scope.createScope("dns.cares.")), stats_(generateCaresDnsResolverStats(*scope_)) {
  AresOptions options = defaultAresOptions();
  initializeChannel(&options.options_, options.optmask_);
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
}

CaresDnsResolverStats DnsResolverImpl::generateCaresDnsResolverStats(Stats::Scope& scope) {
  return {ALL_CARES_DNS_RESOLVER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
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
    resolver_addrs.push_back(fmt::format(fmt::runtime(resolver->ip()->ipv6() ? "[{}]:{}" : "{}:{}"),
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

bool DnsResolverImpl::isCaresDefaultTheOnlyNameserver() {
  struct ares_addr_port_node* servers{};
  int result = ares_get_servers_ports(channel_, &servers);
  RELEASE_ASSERT(result == ARES_SUCCESS, "failure in ares_get_servers_ports");
  // as determined in init_by_defaults in ares_init.c.
  const bool has_only_default_nameserver =
      servers == nullptr || (servers->next == nullptr && servers->family == AF_INET &&
                             servers->addr.addr4.s_addr == htonl(INADDR_LOOPBACK) &&
                             servers->udp_port == 0 && servers->tcp_port == 0);
  if (servers != nullptr) {
    ares_free_data(servers);
  }
  return has_only_default_nameserver;
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  dirty_channel_ = false;

  options->sock_state_cb = [](void* arg, os_fd_t fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);

  if (resolvers_csv_.has_value()) {
    bool use_resolvers = true;
    // If the only name server available is c-ares' default then fallback to the user defined
    // resolvers. Otherwise, use the resolvers provided by c-ares.
    if (use_resolvers_as_fallback_ && !isCaresDefaultTheOnlyNameserver()) {
      use_resolvers = false;
    }

    if (use_resolvers) {
      int result = ares_set_servers_ports_csv(channel_, resolvers_csv_->c_str());
      RELEASE_ASSERT(result == ARES_SUCCESS, "");
    }
  }
}

// Treat responses with `ARES_ENODATA` or `ARES_ENOTFOUND` status as DNS response with no records.
// @see DnsResolverImpl::PendingResolution::onAresGetAddrInfoCallback for details.
bool DnsResolverImpl::AddrInfoPendingResolution::isResponseWithNoRecords(int status) {
  return status == ARES_ENODATA || status == ARES_ENOTFOUND;
}

void DnsResolverImpl::AddrInfoPendingResolution::onAresGetAddrInfoCallback(
    int status, int timeouts, ares_addrinfo* addrinfo) {
  ASSERT(pending_resolutions_ > 0);
  pending_resolutions_--;

  parent_.stats_.resolve_total_.inc();
  parent_.stats_.pending_resolutions_.dec();

  if (status != ARES_SUCCESS) {
    parent_.chargeGetAddrInfoErrorStats(status, timeouts);

    if (!isResponseWithNoRecords(status)) {
      ENVOY_LOG_EVENT(debug, "cares_resolution_failure",
                      "dns resolution for {} failed with c-ares status {}", dns_name_, status);
    } else {
      ENVOY_LOG_EVENT(debug, "cares_resolution_no_records", "dns resolution without records for {}",
                      dns_name_);
    }
  }

  // We receive ARES_EDESTRUCTION when destructing with pending queries.
  if (status == ARES_EDESTRUCTION) {
    // In the destruction path we must wait until there are no more pending queries. Resolution is
    // not truly finished until the last parallel query has been destroyed.
    if (pending_resolutions_ > 0) {
      return;
    }

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
      bool can_process_v4 =
          (!parent_.filter_unroutable_families_ || available_interfaces_.v4_available_);
      bool can_process_v6 =
          (!parent_.filter_unroutable_families_ || available_interfaces_.v6_available_);

      int min_ttl = INT_MAX; // [RFC 2181](https://datatracker.ietf.org/doc/html/rfc2181)
      // Loop through CNAME and get min_ttl
      for (const ares_addrinfo_cname* cname = addrinfo->cnames; cname != nullptr;
           cname = cname->next) {
        min_ttl = std::min(min_ttl, cname->ttl);
      }

      for (const ares_addrinfo_node* ai = addrinfo->nodes; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && can_process_v4) {
          sockaddr_in address;
          memset(&address, 0, sizeof(address));
          address.sin_family = AF_INET;
          address.sin_port = 0;
          address.sin_addr = reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr;

          pending_response_.address_list_.emplace_back(
              DnsResponse(std::make_shared<const Address::Ipv4Instance>(&address),
                          std::chrono::seconds(std::min(min_ttl, ai->ai_ttl))));
        } else if (ai->ai_family == AF_INET6 && can_process_v6) {
          sockaddr_in6 address;
          memset(&address, 0, sizeof(address));
          address.sin6_family = AF_INET6;
          address.sin6_port = 0;
          address.sin6_addr = reinterpret_cast<sockaddr_in6*>(ai->ai_addr)->sin6_addr;
          pending_response_.address_list_.emplace_back(
              DnsResponse(std::make_shared<const Address::Ipv6Instance>(address),
                          std::chrono::seconds(std::min(min_ttl, ai->ai_ttl))));
        }
      }
    }

    if (!pending_response_.address_list_.empty() && dns_lookup_family_ != DnsLookupFamily::All) {
      completed_ = true;
    }

    ASSERT(addrinfo != nullptr);
    ares_freeaddrinfo(addrinfo);
  } else if (isResponseWithNoRecords(status)) {
    // Treat `ARES_ENODATA` or `ARES_ENOTFOUND` here as success to populate back the
    // "empty records" response.
    pending_response_.status_ = ResolutionStatus::Success;
    ASSERT(addrinfo == nullptr);
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
      family_ = AF_INET;
      startResolutionImpl(AF_INET);
    } else if (dns_lookup_family_ == DnsLookupFamily::V4Preferred) {
      family_ = AF_INET6;
      startResolutionImpl(AF_INET6);
    }

    // Note: Nothing can follow this call to getAddrInfo due to deletion of this
    // object upon synchronous resolution.
    return;
  }
}

void DnsResolverImpl::PendingResolution::finishResolve() {
  ENVOY_LOG_EVENT(debug, "cares_dns_resolution_complete",
                  "dns resolution for {} completed with status {}", dns_name_,
                  static_cast<int>(pending_response_.status_));

  if (!cancelled_) {
    // Use a raw try here because it is used in both main thread and filter.
    // Can not convert to use status code as there may be unexpected exceptions in server fuzz
    // tests, which must be handled. Potential exception may come from getAddressWithPort() or
    // portFromTcpUrl().
    // TODO(chaoqin-li1123): remove try catch pattern here once we figure how to handle unexpected
    // exception in fuzz tests.
    TRY_NEEDS_AUDIT {
      callback_(pending_response_.status_, std::move(pending_response_.address_list_));
    }
    END_TRY
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
  } else {
    ENVOY_LOG_EVENT(debug, "cares_dns_callback_cancelled",
                    "dns resolution callback for {} not issued. Cancelled with reason={}",
                    dns_name_, static_cast<int>(cancel_reason_));
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

void DnsResolverImpl::chargeGetAddrInfoErrorStats(int status, int timeouts) {
  switch (status) {
  case ARES_ENODATA:
    ABSL_FALLTHROUGH_INTENDED;
  case ARES_ENOTFOUND:
    stats_.not_found_.inc();
    break;
  case ARES_ETIMEOUT:
    stats_.timeouts_.add(timeouts);
    break;
  default:
    stats_.get_addr_failure_.inc();
  }
}

DnsResolverImpl::AddrInfoPendingResolution::AddrInfoPendingResolution(
    DnsResolverImpl& parent, ResolveCb callback, Event::Dispatcher& dispatcher,
    ares_channel channel, const std::string& dns_name, DnsLookupFamily dns_lookup_family)
    : PendingResolution(parent, callback, dispatcher, channel, dns_name),
      dns_lookup_family_(dns_lookup_family), available_interfaces_(availableInterfaces()) {
  if (dns_lookup_family == DnsLookupFamily::Auto ||
      dns_lookup_family == DnsLookupFamily::V4Preferred) {
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
  case DnsLookupFamily::All:
    family_ = AF_UNSPEC;
    break;
  }
}

DnsResolverImpl::AddrInfoPendingResolution::~AddrInfoPendingResolution() {
  // All pending resolutions should be cleaned up at this point.
  ASSERT(pending_resolutions_ == 0);
}

void DnsResolverImpl::AddrInfoPendingResolution::startResolution() { startResolutionImpl(family_); }

void DnsResolverImpl::AddrInfoPendingResolution::startResolutionImpl(int family) {
  pending_resolutions_++;
  parent_.stats_.pending_resolutions_.inc();
  if (parent_.filter_unroutable_families_ && family != AF_UNSPEC) {
    switch (family) {
    case AF_INET:
      if (!available_interfaces_.v4_available_) {
        ENVOY_LOG_EVENT(debug, "cares_resolution_filtered", "filtered v4 lookup");
        onAresGetAddrInfoCallback(ARES_EBADFAMILY, 0, nullptr);
        return;
      }
      break;
    case AF_INET6:
      if (!available_interfaces_.v6_available_) {
        ENVOY_LOG_EVENT(debug, "cares_resolution_filtered", "filtered v6 lookup");
        onAresGetAddrInfoCallback(ARES_EBADFAMILY, 0, nullptr);
        return;
      }
      break;
    default:
      ENVOY_BUG(false, fmt::format("Unexpected IP family {}", family));
    }
  }

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

DnsResolverImpl::AddrInfoPendingResolution::AvailableInterfaces
DnsResolverImpl::AddrInfoPendingResolution::availableInterfaces() {
  if (!Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    // Maintain no-op behavior if the system cannot provide interface information.
    return {true, true};
  }

  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
  if (rc.return_value_ != 0) {
    ENVOY_LOG_EVENT(debug, "cares_getifaddrs_error",
                    "dns resolution for {} could not obtain interface information with error={}",
                    dns_name_, rc.errno_);
    // Maintain no-op behavior if the system encountered an error while providing interface
    // information.
    return {true, true};
  }

  DnsResolverImpl::AddrInfoPendingResolution::AvailableInterfaces available_interfaces{false,
                                                                                       false};
  for (const auto& interface_address : interface_addresses) {
    if (!interface_address.interface_addr_->ip()) {
      continue;
    }

    if (Network::Utility::isLoopbackAddress(*interface_address.interface_addr_)) {
      continue;
    }

    switch (interface_address.interface_addr_->ip()->version()) {
    case Network::Address::IpVersion::v4:
      available_interfaces.v4_available_ = true;
      if (available_interfaces.v6_available_) {
        return available_interfaces;
      }
      break;
    case Network::Address::IpVersion::v6:
      available_interfaces.v6_available_ = true;
      if (available_interfaces.v4_available_) {
        return available_interfaces;
      }
      break;
    }
  }
  return available_interfaces;
}

// c-ares DNS resolver factory
class CaresDnsResolverFactory : public DnsResolverFactory,
                                public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return std::string(CaresDnsResolver); }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig()};
  }

  DnsResolverSharedPtr createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                                         const envoy::config::core::v3::TypedExtensionConfig&
                                             typed_dns_resolver_config) const override {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    std::vector<Network::Address::InstanceConstSharedPtr> resolvers;

    ASSERT(dispatcher.isThreadSafe());
    // Only c-ares DNS factory will call into this function.
    // Directly unpack the typed config to a c-ares object.
    Envoy::MessageUtil::unpackTo(typed_dns_resolver_config.typed_config(), cares);
    if (!cares.resolvers().empty()) {
      const auto& resolver_addrs = cares.resolvers();
      resolvers.reserve(resolver_addrs.size());
      for (const auto& resolver_addr : resolver_addrs) {
        resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
      }
    }
    return std::make_shared<Network::DnsResolverImpl>(cares, dispatcher, resolvers,
                                                      api.rootScope());
  }

  void initialize() override {
    // Initialize c-ares library in case first time.
    absl::MutexLock lock(&mutex_);
    if (!ares_library_initialized_) {
      ares_library_initialized_ = true;
      ENVOY_LOG(debug, "c-ares library initialized.");
      ares_library_init(ARES_LIB_INIT_ALL);
    }
  }
  void terminate() override {
    // Cleanup c-ares library if initialized.
    absl::MutexLock lock(&mutex_);
    if (ares_library_initialized_) {
      ares_library_initialized_ = false;
      ENVOY_LOG(debug, "c-ares library cleaned up.");
      ares_library_cleanup();
    }
  }

private:
  bool ares_library_initialized_ ABSL_GUARDED_BY(mutex_){false};
  absl::Mutex mutex_;
};

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(CaresDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
