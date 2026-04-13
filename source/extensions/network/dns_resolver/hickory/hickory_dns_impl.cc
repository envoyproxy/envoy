// NOLINT(namespace-envoy)

#include "source/extensions/network/dns_resolver/hickory/hickory_dns_impl.h"

#include "source/common/network/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Network {

namespace {

// The static module name for the Hickory DNS Rust module.
constexpr absl::string_view HickoryModuleName = "hickory_dns_static";

std::string serializeConfigToJson(
    const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
        proto_config) {
  std::string json;
  auto status = Protobuf::util::MessageToJsonString(proto_config, &json);
  ASSERT(status.ok());
  return json;
}

} // namespace

// -- HickoryDnsResolverConfig -------------------------------------------------

std::shared_ptr<HickoryDnsResolverConfig> HickoryDnsResolverConfig::create(
    const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
        proto_config) {
  auto config = std::shared_ptr<HickoryDnsResolverConfig>(new HickoryDnsResolverConfig());

  // The Hickory DNS module is statically linked and always available.
  auto module_or =
      Extensions::DynamicModules::newDynamicModuleByName(HickoryModuleName, /*do_not_close=*/true);
  RELEASE_ASSERT(module_or.ok(), std::string(module_or.status().message()));
  config->dynamic_module_ = std::move(*module_or);

  // All symbols are guaranteed to be present in the statically linked module.
#define RESOLVE_SYMBOL(field, symbol)                                                              \
  {                                                                                                \
    auto fn = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);        \
    RELEASE_ASSERT(fn.ok(), std::string(fn.status().message()));                                   \
    config->field = *fn;                                                                           \
  }

  RESOLVE_SYMBOL(on_dns_resolver_config_new_, "envoy_dynamic_module_on_dns_resolver_config_new");
  RESOLVE_SYMBOL(on_dns_resolver_config_destroy_,
                 "envoy_dynamic_module_on_dns_resolver_config_destroy");
  RESOLVE_SYMBOL(on_dns_resolver_new_, "envoy_dynamic_module_on_dns_resolver_new");
  RESOLVE_SYMBOL(on_dns_resolver_destroy_, "envoy_dynamic_module_on_dns_resolver_destroy");
  RESOLVE_SYMBOL(on_dns_resolve_, "envoy_dynamic_module_on_dns_resolve");
  RESOLVE_SYMBOL(on_dns_resolve_cancel_, "envoy_dynamic_module_on_dns_resolve_cancel");
  RESOLVE_SYMBOL(on_dns_resolver_reset_networking_,
                 "envoy_dynamic_module_on_dns_resolver_reset_networking");

#undef RESOLVE_SYMBOL

  const std::string config_json = serializeConfigToJson(proto_config);

  envoy_dynamic_module_type_envoy_buffer name_buf;
  name_buf.ptr = HickoryModuleName.data();
  name_buf.length = HickoryModuleName.size();

  envoy_dynamic_module_type_envoy_buffer config_buf;
  config_buf.ptr = config_json.c_str();
  config_buf.length = config_json.size();

  config->in_module_config_ = config->on_dns_resolver_config_new_(
      static_cast<envoy_dynamic_module_type_dns_resolver_config_envoy_ptr>(config.get()), name_buf,
      config_buf);
  RELEASE_ASSERT(config->in_module_config_ != nullptr,
                 "Hickory DNS module rejected the configuration.");

  return config;
}

HickoryDnsResolverConfig::~HickoryDnsResolverConfig() {
  on_dns_resolver_config_destroy_(in_module_config_);
}

// -- HickoryPendingResolution -------------------------------------------------

HickoryPendingResolution::HickoryPendingResolution(HickoryDnsResolver& parent,
                                                   DnsResolver::ResolveCb callback,
                                                   uint64_t query_id, const std::string& dns_name)
    : callback_(std::move(callback)), query_id_(query_id), dns_name_(dns_name), parent_(parent) {}

void HickoryPendingResolution::cancel(CancelReason) {
  cancelled_ = true;
  parent_.config_->on_dns_resolve_cancel_(parent_.resolver_module_ptr_, query_module_ptr_);
}

// -- HickoryDnsResolver -------------------------------------------------------

HickoryDnsResolverStats HickoryDnsResolver::generateHickoryDnsResolverStats(Stats::Scope& scope) {
  return {ALL_HICKORY_DNS_RESOLVER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

HickoryDnsResolver::HickoryDnsResolver(HickoryDnsResolverConfigSharedPtr config,
                                       Event::Dispatcher& dispatcher, Stats::Scope& root_scope)
    : config_(std::move(config)), dispatcher_(dispatcher),
      scope_(root_scope.createScope("dns.hickory.")),
      stats_(generateHickoryDnsResolverStats(*scope_)) {
  resolver_module_ptr_ =
      config_->on_dns_resolver_new_(config_->in_module_config_, static_cast<const void*>(this));
  ASSERT(resolver_module_ptr_ != nullptr);
}

HickoryDnsResolver::~HickoryDnsResolver() {
  // Step 1: Set the C++ shutdown flag so the ABI callback called from `Tokio` threads
  // will not post to the dispatcher. This provides TSAN-visible synchronization since
  // the Rust code is not instrumented by TSAN.
  shutting_down_.store(true, std::memory_order_release);

  // Step 2: Destroy the module resolver, which shuts down the `Tokio` runtime and
  // blocks until all worker threads have exited.
  config_->on_dns_resolver_destroy_(resolver_module_ptr_);

  // Step 3: Free Rust-side query objects for all remaining pending queries, and delete
  // the C++ pending resolution objects. Already-cancelled queries have their Rust-side
  // objects freed by the cancel() call, so only free non-cancelled ones. Decrement the
  // pending_resolutions gauge for each query since their callbacks will never arrive.
  for (auto& [id, pending] : pending_queries_) {
    stats_.pending_resolutions_.dec();
    if (!pending->cancelled_) {
      // Safe: on_dns_resolve_cancel_ does not dereference the resolver pointer.
      config_->on_dns_resolve_cancel_(resolver_module_ptr_, pending->query_module_ptr_);
    }
    delete pending;
  }
  pending_queries_.clear();
}

envoy_dynamic_module_type_dns_lookup_family
HickoryDnsResolver::toLookupFamily(DnsLookupFamily dns_lookup_family) {
  switch (dns_lookup_family) {
  case DnsLookupFamily::V4Only:
    return envoy_dynamic_module_type_dns_lookup_family_V4Only;
  case DnsLookupFamily::V6Only:
    return envoy_dynamic_module_type_dns_lookup_family_V6Only;
  case DnsLookupFamily::Auto:
    return envoy_dynamic_module_type_dns_lookup_family_Auto;
  case DnsLookupFamily::V4Preferred:
    return envoy_dynamic_module_type_dns_lookup_family_V4Preferred;
  case DnsLookupFamily::All:
    return envoy_dynamic_module_type_dns_lookup_family_All;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

ActiveDnsQuery* HickoryDnsResolver::resolve(const std::string& dns_name,
                                            DnsLookupFamily dns_lookup_family, ResolveCb callback) {
  ENVOY_LOG(debug, "resolving [{}] via Hickory DNS", dns_name);

  stats_.pending_resolutions_.inc();

  const uint64_t query_id = next_query_id_++;
  auto* pending = new HickoryPendingResolution(*this, std::move(callback), query_id, dns_name);
  pending_queries_[query_id] = pending;

  envoy_dynamic_module_type_envoy_buffer name_buf;
  name_buf.ptr = dns_name.c_str();
  name_buf.length = dns_name.size();

  pending->query_module_ptr_ = config_->on_dns_resolve_(
      resolver_module_ptr_, name_buf, toLookupFamily(dns_lookup_family), query_id);
  ASSERT(pending->query_module_ptr_ != nullptr);
  return pending;
}

void HickoryDnsResolver::resetNetworking() {
  config_->on_dns_resolver_reset_networking_(resolver_module_ptr_);
}

void HickoryDnsResolver::onResolveComplete(uint64_t query_id,
                                           envoy_dynamic_module_type_dns_resolution_status status,
                                           absl::string_view details,
                                           std::list<DnsResponse>&& response) {
  auto it = pending_queries_.find(query_id);
  if (it == pending_queries_.end()) {
    return;
  }

  HickoryPendingResolution* pending = it->second;
  pending_queries_.erase(it);

  stats_.resolve_total_.inc();
  stats_.pending_resolutions_.dec();

  if (pending->cancelled_) {
    ENVOY_LOG(debug, "dropping cancelled query [{}]", pending->dns_name_);
    delete pending;
    return;
  }

  const auto envoy_status = status == envoy_dynamic_module_type_dns_resolution_status_Completed
                                ? ResolutionStatus::Completed
                                : ResolutionStatus::Failure;

  if (envoy_status == ResolutionStatus::Failure) {
    chargeGetAddrInfoErrorStats(details);
    ENVOY_LOG(debug, "Hickory DNS resolution failed for [{}]: {}", pending->dns_name_, details);
  } else {
    ENVOY_LOG(debug, "Hickory DNS resolution complete for [{}]: {} address(es)", pending->dns_name_,
              response.size());
  }

  // Free the Rust-side query object. The cancel ABI function takes ownership and drops it.
  // Calling cancel on an already-completed query is harmless (it only sets the AtomicBool).
  config_->on_dns_resolve_cancel_(resolver_module_ptr_, pending->query_module_ptr_);
  pending->query_module_ptr_ = nullptr;

  pending->callback_(envoy_status, std::string(details), std::move(response));
  delete pending;
}

void HickoryDnsResolver::chargeGetAddrInfoErrorStats(absl::string_view details) {
  // The detail string from the Hickory Rust module contains identifiable patterns from the
  // underlying hickory-resolver library that allow categorizing the error.
  if (absl::StrContains(details, "no record")) {
    stats_.not_found_.inc();
  } else if (absl::StrContains(details, "timed out") || absl::StrContains(details, "timeout")) {
    stats_.timeouts_.inc();
  } else {
    stats_.get_addr_failure_.inc();
  }
}

// -- HickoryDnsResolverFactory ------------------------------------------------

absl::StatusOr<DnsResolverSharedPtr> HickoryDnsResolverFactory::createDnsResolver(
    Event::Dispatcher& dispatcher, Api::Api& api,
    const envoy::config::core::v3::TypedExtensionConfig& typed_config) const {
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig proto_config;
  RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_config.typed_config(), proto_config));

  auto config = HickoryDnsResolverConfig::create(proto_config);
  return std::make_shared<HickoryDnsResolver>(std::move(config), dispatcher, api.rootScope());
}

REGISTER_FACTORY(HickoryDnsResolverFactory, DnsResolverFactory);

} // namespace Network
} // namespace Envoy

// -- ABI Callback Implementation ----------------------------------------------
// This callback may be called from any thread by the Rust module. It copies all
// buffer data synchronously and posts the results to the Envoy dispatcher thread.
void envoy_dynamic_module_callback_dns_resolve_complete(
    envoy_dynamic_module_type_dns_resolver_envoy_ptr resolver_envoy_ptr, uint64_t query_id,
    envoy_dynamic_module_type_dns_resolution_status status,
    envoy_dynamic_module_type_module_buffer details,
    const envoy_dynamic_module_type_dns_address* addresses, size_t num_addresses) {

  // const_cast is safe here: the resolver passed itself as const void* during creation,
  // and we need the mutable reference to post to its dispatcher.
  auto* resolver = const_cast<Envoy::Network::HickoryDnsResolver*>(
      static_cast<const Envoy::Network::HickoryDnsResolver*>(resolver_envoy_ptr));

  // Check the C++ shutdown flag before accessing any resolver state. This provides
  // TSAN-visible synchronization for accesses from `Tokio` worker threads.
  if (resolver->shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  const std::string details_str = (details.ptr != nullptr && details.length > 0)
                                      ? std::string(details.ptr, details.length)
                                      : std::string();
  std::list<Envoy::Network::DnsResponse> response;
  for (size_t i = 0; i < num_addresses; i++) {
    const auto& addr = addresses[i];
    if (addr.address_ptr == nullptr || addr.address_length == 0) {
      continue;
    }
    std::string addr_str(addr.address_ptr, addr.address_length);
    auto address = Envoy::Network::Utility::parseInternetAddressAndPortNoThrow(addr_str);
    if (address != nullptr) {
      response.emplace_back(
          Envoy::Network::DnsResponse(address, std::chrono::seconds(addr.ttl_seconds)));
    }
  }

  resolver->dispatcher_.post([resolver, query_id, status, details = std::move(details_str),
                              response = std::move(response)]() mutable {
    resolver->onResolveComplete(query_id, status, details, std::move(response));
  });
}
