#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/network/dns_resolver.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {

// This resolver uses getaddrinfo() on a dedicated resolution thread. Thus, it is only suitable
// currently for relatively low rate resolutions. In the future a thread pool could be added if
// desired.
class GetAddrInfoDnsResolver : public DnsResolver, public Logger::Loggable<Logger::Id::dns> {
public:
  GetAddrInfoDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api)
      : dispatcher_(dispatcher),
        resolver_thread_(api.threadFactory().createThread([this] { resolveThreadRoutine(); })) {}

  ~GetAddrInfoDnsResolver() override {
    {
      absl::MutexLock guard(&mutex_);
      shutting_down_ = true;
    }

    resolver_thread_->join();
  }

  // DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override {
    ENVOY_LOG(debug, "adding new query [{}] to pending queries", dns_name);
    auto new_query = std::make_unique<PendingQuery>(dns_name, dns_lookup_family, callback);
    absl::MutexLock guard(&mutex_);
    pending_queries_.emplace_back(std::move(new_query));
    return pending_queries_.back().get();
  }

  void resetNetworking() override {}

private:
  class PendingQuery : public ActiveDnsQuery {
  public:
    PendingQuery(const std::string& dns_name, DnsLookupFamily dns_lookup_family, ResolveCb callback)
        : dns_name_(dns_name), dns_lookup_family_(dns_lookup_family), callback_(callback) {}

    void cancel(CancelReason) override {
      ENVOY_LOG(debug, "cancelling query [{}]", dns_name_);
      cancelled_ = true;
    }

    const std::string dns_name_;
    const DnsLookupFamily dns_lookup_family_;
    ResolveCb callback_;
    bool cancelled_{false};
  };
  // Must be a shared_ptr for passing around via post.
  using PendingQuerySharedPtr = std::shared_ptr<PendingQuery>;

  // RAII wrapper to free the `addrinfo`.
  class AddrInfoWrapper : NonCopyable {
  public:
    AddrInfoWrapper(addrinfo* info) : info_(info) {}
    ~AddrInfoWrapper() {
      if (info_ != nullptr) {
        Api::OsSysCallsSingleton::get().freeaddrinfo(info_);
      }
    }
    const addrinfo* get() { return info_; }

  private:
    addrinfo* info_;
  };

  // Parse a getaddrinfo() response and determine the final address list. We could potentially avoid
  // adding v4 or v6 addresses if we know they will never be used. Right now the final filtering is
  // done below and this code is kept simple.
  std::pair<ResolutionStatus, std::list<DnsResponse>>
  processResponse(const PendingQuery& query, const addrinfo* addrinfo_result) {
    std::list<DnsResponse> v4_results;
    std::list<DnsResponse> v6_results;
    for (auto ai = addrinfo_result; ai != nullptr; ai = ai->ai_next) {
      if (ai->ai_family == AF_INET) {
        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr = reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr;

        v4_results.emplace_back(
            DnsResponse(std::make_shared<const Address::Ipv4Instance>(&address), DEFAULT_TTL));
      } else if (ai->ai_family == AF_INET6) {
        sockaddr_in6 address;
        memset(&address, 0, sizeof(address));
        address.sin6_family = AF_INET6;
        address.sin6_port = 0;
        address.sin6_addr = reinterpret_cast<sockaddr_in6*>(ai->ai_addr)->sin6_addr;
        v6_results.emplace_back(
            DnsResponse(std::make_shared<const Address::Ipv6Instance>(address), DEFAULT_TTL));
      }
    }

    std::list<DnsResponse> final_results;
    switch (query.dns_lookup_family_) {
    case DnsLookupFamily::All: {
      final_results = std::move(v4_results);
      final_results.splice(final_results.begin(), v6_results);
      break;
    }
    case DnsLookupFamily::V4Only: {
      final_results = std::move(v4_results);
      break;
    }
    case DnsLookupFamily::V6Only: {
      final_results = std::move(v6_results);
      break;
    }
    case DnsLookupFamily::V4Preferred: {
      if (!v4_results.empty()) {
        final_results = std::move(v4_results);
      } else {
        final_results = std::move(v6_results);
      }
      break;
    }
    case DnsLookupFamily::Auto: {
      // This is effectively V6Preferred.
      if (!v6_results.empty()) {
        final_results = std::move(v6_results);
      } else {
        final_results = std::move(v4_results);
      }
      break;
    }
    }

    ENVOY_LOG(debug, "getaddrinfo resolution complete for host '{}': {}", query.dns_name_,
              accumulateToString<Network::DnsResponse>(final_results, [](const auto& dns_response) {
                return dns_response.addrInfo().address_->asString();
              }));

    return std::make_pair(ResolutionStatus::Success, final_results);
  }

  // Background thread which wakes up and does resolutions.
  void resolveThreadRoutine() {
    ENVOY_LOG(debug, "starting getaddrinfo resolver thread");

    while (true) {
      PendingQuerySharedPtr next_query;
      {
        absl::MutexLock guard(&mutex_);
        auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
          return shutting_down_ || !pending_queries_.empty();
        };
        mutex_.Await(absl::Condition(&condition));
        if (shutting_down_) {
          break;
        }

        next_query = std::move(pending_queries_.front());
        pending_queries_.pop_front();
      }

      ENVOY_LOG(debug, "popped pending query [{}]", next_query->dns_name_);

      // For mock testing make sure the getaddrinfo() response is freed prior to the post.
      std::pair<ResolutionStatus, std::list<DnsResponse>> response;
      {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_ADDRCONFIG;
        hints.ai_family = AF_UNSPEC;
        // If we don't specify a socket type, every address will appear twice, once
        // for SOCK_STREAM and one for SOCK_DGRAM. Since we do not return the family
        // anyway, just pick one.
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addrinfo_result_do_not_use = nullptr;
        auto rc = Api::OsSysCallsSingleton::get().getaddrinfo(
            next_query->dns_name_.c_str(), nullptr, &hints, &addrinfo_result_do_not_use);
        auto addrinfo_wrapper = AddrInfoWrapper(addrinfo_result_do_not_use);
        if (rc.return_value_ == 0) {
          response = processResponse(*next_query, addrinfo_wrapper.get());
        } else {
          // TODO(mattklein123): Handle some errors differently such as `EAI_NODATA`.
          ENVOY_LOG(debug, "getaddrinfo failed with rc={} errno={}", gai_strerror(rc.return_value_),
                    errorDetails(rc.errno_));
          response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        }
      }

      dispatcher_.post(
          [finished_query = std::move(next_query), response = std::move(response)]() mutable {
            if (finished_query->cancelled_) {
              ENVOY_LOG(debug, "dropping cancelled query [{}]", finished_query->dns_name_);
            } else {
              finished_query->callback_(response.first, std::move(response.second));
            }
          });
    }

    ENVOY_LOG(debug, "getaddrinfo resolver thread exiting");
  }

  // getaddrinfo() doesn't provide TTL so use a hard coded default. This can be made configurable
  // later if needed.
  static constexpr std::chrono::seconds DEFAULT_TTL = std::chrono::seconds(60);

  Event::Dispatcher& dispatcher_;
  absl::Mutex mutex_;
  std::list<PendingQuerySharedPtr> pending_queries_ ABSL_GUARDED_BY(mutex_);
  bool shutting_down_ ABSL_GUARDED_BY(mutex_){};
  // The resolver thread must be initialized last so that the above members are already fully
  // initialized.
  const Thread::ThreadPtr resolver_thread_;
};

// getaddrinfo DNS resolver factory
class GetAddrInfoDnsResolverFactory : public DnsResolverFactory,
                                      public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return {"envoy.network.dns_resolver.getaddrinfo"}; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::network::dns_resolver::getaddrinfo::v3::
                                         GetAddrInfoDnsResolverConfig()};
  }

  DnsResolverSharedPtr
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig&) const override {
    return std::make_shared<GetAddrInfoDnsResolver>(dispatcher, api);
  }
};

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(GetAddrInfoDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
