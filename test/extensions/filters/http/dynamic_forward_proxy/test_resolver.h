#pragma once

#include "envoy/network/dns_resolver.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/extensions/filters/http/dynamic_forward_proxy/test_resolver.pb.h"

namespace Envoy {
namespace Network {

// A test resolver which blocks resolution until unblockResolve is called.
class TestResolver : public GetAddrInfoDnsResolver {
public:
  using GetAddrInfoDnsResolver::GetAddrInfoDnsResolver;

  static void unblockResolve() {
    while (1) {
      absl::MutexLock guard(&resolution_mutex_);
      if (blocked_resolutions_.empty()) {
        break;
      }
      auto run = blocked_resolutions_.front();
      blocked_resolutions_.pop_front();
      run();
    }
  }

  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override {
    auto new_query = new PendingQuery(dns_name, dns_lookup_family, callback, mutex_);

    absl::MutexLock guard(&resolution_mutex_);
    blocked_resolutions_.push_back([&]() {
      absl::MutexLock guard(&mutex_);
      pending_queries_.emplace_back(std::unique_ptr<PendingQuery>{new_query});
    });
    return new_query;
  }

  static absl::Mutex resolution_mutex_;
  static std::list<std::function<void()>> blocked_resolutions_ ABSL_GUARDED_BY(resolution_mutex_);
};

} // namespace Network
} // namespace Envoy
