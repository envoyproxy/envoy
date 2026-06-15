#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#ifdef __ANDROID__
#include <android/multinetwork.h>
#include <dlfcn.h>
#include <poll.h>

#include "source/extensions/network/dns_resolver/getaddrinfo/dns_packet_parser.h"
#endif

namespace Envoy {
namespace Network {

#ifdef __ANDROID__
namespace {
int defaultAndroidResNquery(net_handle_t network, const char* dname, int ns_class, int ns_type,
                            uint32_t flags) {
  typedef int (*pfn_android_res_nquery)(net_handle_t network, const char* dname, int ns_class,
                                        int ns_type, uint32_t flags);
  static pfn_android_res_nquery fn = nullptr;
  static bool searched = false;
  if (!searched) {
    void* handle = dlopen("libc.so", RTLD_NOW | RTLD_LOCAL);
    if (handle != nullptr) {
      fn = reinterpret_cast<pfn_android_res_nquery>(dlsym(handle, "android_res_nquery"));
    }
    searched = true;
  }
  if (fn != nullptr) {
    return fn(network, dname, ns_class, ns_type, flags);
  }
  errno = ENOSYS;
  return -1;
}

int defaultAndroidResNresult(int fd, int* rcode, uint8_t* answer, size_t anslen) {
  typedef int (*pfn_android_res_nresult)(int fd, int* rcode, uint8_t* answer, size_t anslen);
  static pfn_android_res_nresult fn = nullptr;
  static bool searched = false;
  if (!searched) {
    void* handle = dlopen("libc.so", RTLD_NOW | RTLD_LOCAL);
    if (handle != nullptr) {
      fn = reinterpret_cast<pfn_android_res_nresult>(dlsym(handle, "android_res_nresult"));
    }
    searched = true;
  }
  if (fn != nullptr) {
    return fn(fd, rcode, answer, anslen);
  }
  errno = ENOSYS;
  return -1;
}
} // namespace

// Global pointers that can be overridden in tests.
int (*android_res_nquery_ptr)(net_handle_t network, const char* dname, int ns_class, int ns_type,
                              uint32_t flags) = defaultAndroidResNquery;
int (*android_res_nresult_ptr)(int fd, int* rcode, uint8_t* answer,
                               size_t anslen) = defaultAndroidResNresult;
#endif

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

constexpr uint32_t kThreadCap = 10;

GetAddrInfoDnsResolver::GetAddrInfoDnsResolver(
    const envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig&
        config,
    Event::Dispatcher& dispatcher, Api::Api& api)
    : config_(config), dispatcher_(dispatcher), api_(api) {
  uint32_t num_threads =
      config_.has_num_resolver_threads() ? config_.num_resolver_threads().value() : 1;
  num_threads = std::min(num_threads, kThreadCap);
  ENVOY_LOG(trace, "Starting getaddrinfo resolver with {} threads", num_threads);
  resolver_threads_.reserve(num_threads);
  for (uint32_t i = 0; i < num_threads; i++) {
    resolver_threads_.emplace_back(
        api_.threadFactory().createThread([this] { resolveThreadRoutine(); }));
  }
}

GetAddrInfoDnsResolver::~GetAddrInfoDnsResolver() {
  {
    absl::MutexLock guard(mutex_);
    shutting_down_ = true;
    pending_queries_.clear();
  }

  for (auto& thread : resolver_threads_) {
    thread->join();
  }
  ENVOY_LOG(trace, "All getaddrinfo resolver threads joined");
}

ActiveDnsQuery* GetAddrInfoDnsResolver::resolve(const std::string& dns_name,
                                                DnsLookupFamily dns_lookup_family,
                                                ResolveCb callback) {
  ENVOY_LOG(trace, "adding new query [{}] to pending queries", dns_name);
  auto new_query = std::make_unique<PendingQuery>(dns_name, dns_lookup_family, std::move(callback));
  new_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NotStarted));
  ActiveDnsQuery* active_query = new_query.get();
  {
    absl::MutexLock guard(mutex_);
    if (config_.has_num_retries()) {
      // + 1 to include the initial query.
      pending_queries_.push_back({std::move(new_query), config_.num_retries().value() + 1});
    } else {
      pending_queries_.push_back({std::move(new_query), absl::nullopt});
    }
  }
  return active_query;
}

ActiveDnsQuery* GetAddrInfoDnsResolver::resolveRecord(const std::string& dns_name,
                                                      RecordType record_type,
                                                      ResolveRecordCb callback) {
  ENVOY_LOG(trace, "adding new record query [{}] type={} to pending queries", dns_name,
            static_cast<int>(record_type));
  auto new_query = std::make_unique<PendingQuery>(dns_name, record_type, std::move(callback));
  new_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NotStarted));
  ActiveDnsQuery* active_query = new_query.get();
  {
    absl::MutexLock guard(mutex_);
    if (config_.has_num_retries()) {
      pending_queries_.push_back({std::move(new_query), config_.num_retries().value() + 1});
    } else {
      pending_queries_.push_back({std::move(new_query), absl::nullopt});
    }
  }
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

  ENVOY_LOG(trace, "getaddrinfo resolution complete for host '{}': {}", query.dns_name_,
            accumulateToString<Network::DnsResponse>(final_results, [](const auto& dns_response) {
              return dns_response.addrInfo().address_->asString();
            }));

  return std::make_pair(ResolutionStatus::Completed, final_results);
}

// Background thread which wakes up and does resolutions.
void GetAddrInfoDnsResolver::resolveThreadRoutine() {
  ENVOY_LOG(trace, "starting getaddrinfo resolver thread");

  while (true) {
    std::unique_ptr<PendingQuery> next_query;
    absl::optional<uint32_t> num_retries;
    {
      absl::MutexLock guard(mutex_);
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
      if (next_query->isCancelled()) {
        continue;
      }
    }

    ENVOY_LOG(trace, "Thread ({}) popped pending query [{}]",
              api_.threadFactory().currentThreadId().getId(), next_query->dns_name_);

    // For mock testing make sure the getaddrinfo() response is freed prior to the post.
    std::pair<ResolutionStatus, std::list<DnsResponse>> response;
    std::string details;

    if (next_query->is_record_query_) {
      if (next_query->record_type_ == RecordType::HTTPS) {
#if defined(__ANDROID__)
        uint64_t network = 0; // NETWORK_UNSPECIFIED
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Starting));

        auto rc_val = android_res_nquery_ptr(static_cast<net_handle_t>(network),
                                             next_query->dns_name_.c_str(), 1 /* ns_c_in */,
                                             65 /* HTTPS */, 0);
        if (rc_val >= 0) {
          os_fd_t fd = rc_val;
          pollfd pfd;
          pfd.fd = fd;
          pfd.events = POLLIN;
          pfd.revents = 0;

          int poll_rc = ::poll(&pfd, 1, 5000); // 5s timeout
          if (poll_rc > 0 && (pfd.revents & POLLIN)) {
            int rcode = 0;
            std::vector<uint8_t> answer(4096);
            auto res_rc_val = android_res_nresult_ptr(fd, &rcode, answer.data(), answer.size());
            ::close(fd);

            if (res_rc_val >= 0) {
              size_t len = res_rc_val;
              answer.resize(len);

              DnsPacketParser parser(std::move(answer));
              std::list<DnsResponse> records = parser.parseHttpsRecords();
              if (!records.empty()) {
                next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Success));
                response = std::make_pair(ResolutionStatus::Completed, std::move(records));
              } else {
                next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NoResult));
                response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
              }
            } else {
              ENVOY_LOG(debug, "android_res_nresult failed: rc={} errno={}", res_rc_val, errno);
              next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
              response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
            }
          } else {
            ::close(fd);
            ENVOY_LOG(debug, "poll failed or timed out for query [{}]", next_query->dns_name_);
            next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
            response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
          }
        } else {
          ENVOY_LOG(debug, "android_res_nquery failed: rc={} errno={}", rc_val, errno);
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
          response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        }
#else
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
        response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        details = "Unsupported platform for HTTPS resolution";
#endif
      } else {
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
        response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        details = "Unsupported record type";
      }
    } else {
      {
        next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Starting));
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.getaddrinfo_no_ai_flags")) {
          hints.ai_flags = AI_ADDRCONFIG;
        }
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addrinfo_result_do_not_use = nullptr;
        auto rc = Api::OsSysCallsSingleton::get().getaddrinfo(
            next_query->dns_name_.c_str(), nullptr, &hints, &addrinfo_result_do_not_use);
        auto addrinfo_wrapper = AddrInfoWrapper(addrinfo_result_do_not_use);
        if (rc.return_value_ == 0) {
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Success));
          response = processResponse(*next_query, addrinfo_wrapper.get());
        } else if (rc.return_value_ == EAI_AGAIN) {
          if (!num_retries.has_value()) {
            ENVOY_LOG(trace, "retrying query [{}]", next_query->dns_name_);
            next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Retrying));
            {
              absl::MutexLock guard(mutex_);
              pending_queries_.push_back({std::move(next_query), absl::nullopt});
            }
            continue;
          }
          (*num_retries)--;
          if (*num_retries > 0) {
            ENVOY_LOG(trace, "retrying query [{}], num_retries: {}", next_query->dns_name_,
                      *num_retries);
            next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Retrying));
            {
              absl::MutexLock guard(mutex_);
              pending_queries_.push_back({std::move(next_query), *num_retries});
            }
            continue;
          }
          ENVOY_LOG(debug, "not retrying query [{}] because num_retries: {}", next_query->dns_name_,
                    *num_retries);
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::DoneRetrying));
          response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        } else if (rc.return_value_ == EAI_NONAME || rc.return_value_ == EAI_NODATA) {
          ENVOY_LOG(debug, "getaddrinfo for host={} has no results rc={}", next_query->dns_name_,
                    gai_strerror(rc.return_value_));
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::NoResult));
          response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        } else {
          ENVOY_LOG(debug, "getaddrinfo failed for host={} with rc={} errno={}",
                    next_query->dns_name_, gai_strerror(rc.return_value_), errorDetails(rc.errno_));
          next_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Failed));
          response = std::make_pair(ResolutionStatus::Failure, std::list<DnsResponse>());
        }
        details = gai_strerror(rc.return_value_);
      }
    }

    dispatcher_.post([finished_query = std::move(next_query), response = std::move(response),
                      details = std::string(details)]() mutable {
      if (finished_query->isCancelled()) {
        finished_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Cancelled));
        ENVOY_LOG(trace, "dropping cancelled query [{}]", finished_query->dns_name_);
      } else {
        finished_query->addTrace(static_cast<uint8_t>(GetAddrInfoTrace::Callback));
        finished_query->callback_(response.first, std::move(details), std::move(response.second));
      }
    });
  }

  ENVOY_LOG(trace, "getaddrinfo resolver thread exiting");
}

// Register the CaresDnsResolverFactory
REGISTER_FACTORY(GetAddrInfoDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy
