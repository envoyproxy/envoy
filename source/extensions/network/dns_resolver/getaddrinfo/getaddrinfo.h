#pragma once

#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/network/dns_resolver.h"
#include "envoy/registry/registry.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {

DECLARE_FACTORY(GetAddrInfoDnsResolverFactory);

// Trace information for getaddrinfo.
enum class GetAddrInfoTrace : uint8_t {
  NotStarted = 0,
  Starting = 1,
  Success = 2,
  Failed = 3,
  NoResult = 4,
  Retrying = 5,
  DoneRetrying = 6,
  Cancelled = 7,
  Callback = 8,
};

// This resolver uses getaddrinfo() on a dedicated resolution thread. Thus, it is only suitable
// currently for relatively low rate resolutions. In the future a thread pool could be added if
// desired.
class GetAddrInfoDnsResolver : public DnsResolver, public Logger::Loggable<Logger::Id::dns> {
public:
  GetAddrInfoDnsResolver(
      const envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig&
          config,
      Event::Dispatcher& dispatcher, Api::Api& api)
      : config_(config), dispatcher_(dispatcher),
        resolver_thread_(api.threadFactory().createThread([this] { resolveThreadRoutine(); })) {}

  ~GetAddrInfoDnsResolver() override;

  // DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;
  void resetNetworking() override {}

protected:
  class PendingQuery : public ActiveDnsQuery {
  public:
    PendingQuery(const std::string& dns_name, DnsLookupFamily dns_lookup_family, ResolveCb callback)
        : dns_name_(dns_name), dns_lookup_family_(dns_lookup_family), callback_(callback) {}

    void cancel(CancelReason) override {
      ENVOY_LOG(debug, "cancelling query [{}]", dns_name_);
      absl::MutexLock lock(&mutex_);
      cancelled_ = true;
    }

    void addTrace(uint8_t trace) override {
      absl::MutexLock lock(&mutex_);
      traces_.push_back(
          Trace{trace, std::chrono::steady_clock::now()}); // NO_CHECK_FORMAT(real_time)
    }

    std::string getTraces() override {
      absl::MutexLock lock(&mutex_);
      std::vector<std::string> string_traces;
      string_traces.reserve(traces_.size());
      std::transform(traces_.begin(), traces_.end(), std::back_inserter(string_traces),
                     [](const Trace& trace) {
                       return absl::StrCat(trace.trace_, "=",
                                           trace.time_.time_since_epoch().count());
                     });
      return absl::StrJoin(string_traces, ",");
    }

    bool isCancelled() {
      absl::MutexLock lock(&mutex_);
      return cancelled_;
    }

    absl::Mutex mutex_;
    const std::string dns_name_;
    const DnsLookupFamily dns_lookup_family_;
    ResolveCb callback_;
    bool cancelled_{false};
    std::vector<Trace> traces_;
  };

  struct PendingQueryInfo {
    std::unique_ptr<PendingQuery> pending_query_;
    // Empty means it will retry indefinitely until it succeeds.
    absl::optional<uint32_t> num_retries_;
  };

  // Parse a getaddrinfo() response and determine the final address list. We could potentially avoid
  // adding v4 or v6 addresses if we know they will never be used. Right now the final filtering is
  // done below and this code is kept simple.
  std::pair<ResolutionStatus, std::list<DnsResponse>>
  processResponse(const PendingQuery& query, const addrinfo* addrinfo_result);

  // Background thread which wakes up and does resolutions.
  void resolveThreadRoutine();

  // getaddrinfo() doesn't provide TTL so use a hard coded default. This can be made configurable
  // later if needed.
  static constexpr std::chrono::seconds DEFAULT_TTL = std::chrono::seconds(60);

  envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config_;
  Event::Dispatcher& dispatcher_;
  absl::Mutex mutex_;
  std::list<PendingQueryInfo> pending_queries_ ABSL_GUARDED_BY(mutex_);
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

  absl::StatusOr<DnsResolverSharedPtr>
  createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                    const envoy::config::core::v3::TypedExtensionConfig& typed_getaddrinfo_config)
      const override {
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config;
    RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_getaddrinfo_config.typed_config(), config));
    return std::make_shared<GetAddrInfoDnsResolver>(config, dispatcher, api);
  }
};

} // namespace Network
} // namespace Envoy
