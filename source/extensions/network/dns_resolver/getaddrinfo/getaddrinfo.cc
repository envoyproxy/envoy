#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

namespace Envoy {
namespace Network {
namespace {

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

} // namespace

GetAddrInfoDnsResolver::~GetAddrInfoDnsResolver() {
  {
    absl::MutexLock guard(&mutex_);
    shutting_down_ = true;
    pending_queries_.clear();
  }

  resolver_thread_->join();
}

ActiveDnsQuery* GetAddrInfoDnsResolver::resolve(const std::string& dns_name,
                                                DnsLookupFamily dns_lookup_family,
                                                ResolveCb callback) {
  ENVOY_LOG(debug, "adding new query [{}] to pending queries", dns_name);
  auto new_query = std::make_unique<PendingQuery>(dns_name, dns_lookup_family, callback);
  ActiveDnsQuery* active_query;
  {
    absl::MutexLock guard(&mutex_);
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.getaddrinfo_num_retries") &&
        config_.has_num_retries()) {
      // + 1 to include the initial query.
      pending_queries_.push_back({std::move(new_query), config_.num_retries().value() + 1});
    } else {
      pending_queries_.push_back({std::move(new_query), absl::nullopt});
    }
    active_query = pending_queries_.back().pending_query_.get();
  }
  active_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NotStarted));
  return active_query;
}

std::pair<DnsResolver::ResolutionStatus, std::list<DnsResponse>>
GetAddrInfoDnsResolver::processResponse(const PendingQuery& query,
                                        const addrinfo* addrinfo_result) {
  std::list<DnsResponse> v4_results;
  std::list<DnsResponse> v6_results;
  // We could potentially avoid adding v4 or v6 addresses if we know they will never be used.
  // Right now the final filtering is done below and this code is kept simple.
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
  case DnsLookupFamily::All:
    final_results = std::move(v4_results);
    final_results.splice(final_results.begin(), v6_results);
    break;
  case DnsLookupFamily::V4Only:
    final_results = std::move(v4_results);
    break;
  case DnsLookupFamily::V6Only:
    final_results = std::move(v6_results);
    break;
  case DnsLookupFamily::V4Preferred:
    if (!v4_results.empty()) {
      final_results = std::move(v4_results);
    } else {
      final_results = std::move(v6_results);
    }
    break;
  case DnsLookupFamily::Auto:
    // This is effectively V6Preferred.
    if (!v6_results.empty()) {
      final_results = std::move(v6_results);
    } else {
      final_results = std::move(v4_results);
    }
    break;
  }

  ENVOY_LOG(debug, "getaddrinfo resolution complete for host '{}': {}", query.dns_name_,
            accumulateToString<Network::DnsResponse>(final_results, [](const auto& dns_response) {
              return dns_response.addrInfo().address_->asString();
            }));

  return std::make_pair(ResolutionStatus::Completed, final_results);
}

// Background thread which wakes up and does resolutions.
void GetAddrInfoDnsResolver::resolveThreadRoutine() {
  ENVOY_LOG(debug, "starting getaddrinfo resolver thread");

  while (true) {
    std::unique_ptr<PendingQuery> next_query;
    absl::optional<uint32_t> num_retries;
    const bool reresolve =
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.dns_reresolve_on_eai_again");
    const bool treat_nodata_noname_as_success =
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.dns_nodata_noname_is_success");
    {
      absl::MutexLock guard(&mutex_);
      auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
        return shutting_down_ || !pending_queries_.empty();
      };
      mutex_.Await(absl::Condition(&condition));
      if (shutting_down_) {
        break;
      }

      PendingQueryInfo pending_query_info = std::move(pending_queries_.front());
      next_query = std::move(pending_query_info.pending_query_);
      num_retries = pending_query_info.num_retries_;
      pending_queries_.pop_front();
      if (reresolve && next_query->isCancelled()) {
        continue;
      }
    }

    ENVOY_LOG(debug, "popped pending query [{}]", next_query->dns_name_);

    // For mock testing make sure the getaddrinfo() response is freed prior to the post.
    std::pair<ResolutionStatus, std::list<DnsResponse>> response;
    std::string details;
    {
      next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Starting));
      addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_flags = AI_ADDRCONFIG;
      hints.ai_family = AF_UNSPEC;
      // If we don't specify a socket type, every address will appear twice, once
      // for SOCK_STREAM and one for SOCK_DGRAM. Since we do not return the family
      // anyway, just pick one.
      hints.ai_socktype = SOCK_STREAM;
      addrinfo* addrinfo_result_do_not_use = nullptr;
      auto rc = Api::OsSysCallsSingleton::get().getaddrinfo(next_query->dns_name_.c_str(), nullptr,
                                                            &hints, &addrinfo_result_do_not_use);
      auto addrinfo_wrapper = AddrInfoWrapper(addrinfo_result_do_not_use);
      if (rc.return_value_ == 0) {
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Success));
        response = processResponse(*next_query, addrinfo_wrapper.get());
      } else if (reresolve && rc.return_value_ == EAI_AGAIN) {
        if (num_retries.has_value()) {
          (*num_retries)--;
        }
        if (!num_retries.has_value()) {
          ENVOY_LOG(debug, "retrying query [{}]", next_query->dns_name_);
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Retrying));
          {
            absl::MutexLock guard(&mutex_);
            pending_queries_.push_back({std::move(next_query), absl::nullopt});
          }
          continue;
        }
        if (*num_retries > 0) {
          ENVOY_LOG(debug, "retrying query [{}], num_retries: {}", next_query->dns_name_,
                    *num_retries);
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Retrying));
          {
            absl::MutexLock guard(&mutex_);
            pending_queries_.push_back({std::move(next_query), *num_retries});
          }
          continue;
        }
        ENVOY_LOG(debug, "not retrying query [{}] because num_retries: {}", next_query->dns_name_,
                  *num_retries);
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::DoneRetrying));
        response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
      } else if (treat_nodata_noname_as_success &&
                 (rc.return_value_ == EAI_NONAME || rc.return_value_ == EAI_NODATA)) {
        // Treat NONAME and NODATA as DNS records with no results.
        // NODATA and NONAME are typically not transient failures, so we don't expect success if
        // the DNS query is retried.
        // NOTE: this is also how the c-ares resolver treats NONAME and NODATA:
        // https://github.com/envoyproxy/envoy/blob/099d85925b32ce8bf06e241ee433375a0a3d751b/source/extensions/network/dns_resolver/cares/dns_impl.h#L109-L111.
        ENVOY_LOG(debug, "getaddrinfo for host={} has no results rc={}", next_query->dns_name_,
                  gai_strerror(rc.return_value_));
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NoResult));
        response = std::make_pair(ResolutionStatus::Completed, std::list<DnsResponse>());
      } else {
        ENVOY_LOG(debug, "getaddrinfo failed for host={} with rc={} errno={}",
                  next_query->dns_name_, gai_strerror(rc.return_value_), errorDetails(rc.errno_));
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
        response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
      }
      details = gai_strerror(rc.return_value_);
    }

    dispatcher_.post([finished_query = std::move(next_query), response = std::move(response),
                      details = std::string(details)]() mutable {
      if (finished_query->isCancelled()) {
        finished_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Cancelled));
        ENVOY_LOG(debug, "dropping cancelled query [{}]", finished_query->dns_name_);
      } else {
        finished_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Callback));
        finished_query->callback_(response.first, std::move(details), std::move(response.second));
      }
    });
  }

  ENVOY_LOG(debug, "getaddrinfo resolver thread exiting");
}

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(GetAddrInfoDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
