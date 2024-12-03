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
  ~TestResolver() {
    absl::MutexLock guard(&mutex_);
    blocked_resolutions_.clear();
  }

  using GetAddrInfoDnsResolver::GetAddrInfoDnsResolver;

  static void unblockResolve(absl::optional<std::string> dns_override = {}) {
    while (1) {
      absl::MutexLock guard(&resolution_mutex_);
      if (blocked_resolutions_.empty()) {
        continue;
      }
      auto run = std::move(blocked_resolutions_.front());
      blocked_resolutions_.pop_front();
      run(dns_override);
      return;
    }
  }

  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override {
    std::unique_ptr<PendingQuery> new_query =
        std::make_unique<PendingQuery>(dns_name, dns_lookup_family, callback);
    PendingQuery* raw_new_query = new_query.get();
    absl::MutexLock guard(&resolution_mutex_);
    blocked_resolutions_.push_back(
        [&, query = std::move(new_query)](absl::optional<std::string> dns_override) mutable {
          absl::MutexLock guard(&mutex_);
          if (dns_override.has_value()) {
            *const_cast<std::string*>(&query->dns_name_) = dns_override.value();
          }
          // Add a dummy trace for test coverage.
          query->addTrace(100);
          pending_queries_.push_back(PendingQueryInfo{std::move(query), absl::nullopt});
        });
    return raw_new_query;
  }

  static absl::Mutex resolution_mutex_;
  static std::list<absl::AnyInvocable<void(absl::optional<std::string> dns_override)>>
      blocked_resolutions_ ABSL_GUARDED_BY(resolution_mutex_);
};

class TestResolverFactory : public DnsResolverFactory, public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return {"envoy.network.dns_resolver.test_resolver"}; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::test_resolver::v3::TestResolverConfig()};
  }

  absl::StatusOr<DnsResolverSharedPtr>
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig& typed_getaddrinfo_config)
      const override {
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config;
    RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_getaddrinfo_config.typed_config(), config));
    return std::make_shared<TestResolver>(config, dispatcher, api);
  }
};

class OverrideAddrInfoDnsResolverFactory : public Network::GetAddrInfoDnsResolverFactory {
public:
  absl::StatusOr<Network::DnsResolverSharedPtr>
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig& typed_getaddrinfo_config)
      const override {
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config;
    RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_getaddrinfo_config.typed_config(), config));
    return std::make_shared<Network::TestResolver>(config, dispatcher, api);
  }
};

} // namespace Network
} // namespace Envoy
