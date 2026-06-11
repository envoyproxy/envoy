// NOLINT(namespace-envoy)

#include "source/extensions/network/dns_resolver/hickory/hickory_dns_impl.h"

#include "source/common/network/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {

namespace {

// The static module name for the Hickory DNS Rust module.
constexpr absl::string_view HickoryModuleName = "hickory_dns_static";

absl::StatusOr<std::string> serializeConfigToJson(
    const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
        proto_config) {
  std::string json;
  auto status = Protobuf::util::MessageToJsonString(proto_config, &json);
  if (!status.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize HickoryDnsResolverConfig to JSON: ", status.message()));
  }
  return json;
}

} // namespace

// -- HickoryDnsResolverConfig -------------------------------------------------

absl::StatusOr<std::shared_ptr<HickoryDnsResolverConfig>> HickoryDnsResolverConfig::create(
    const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
        proto_config) {
  return createForModule(proto_config, HickoryModuleName);
}

absl::StatusOr<std::shared_ptr<HickoryDnsResolverConfig>> HickoryDnsResolverConfig::createForModule(
    const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
        proto_config,
    absl::string_view module_name) {
  auto config = std::shared_ptr<HickoryDnsResolverConfig>(new HickoryDnsResolverConfig());

  // The Hickory DNS module is statically linked and always available in production. Tests may
  // override ``module_name`` to exercise load- and symbol-resolution-failure paths.
  auto module_or =
      Extensions::DynamicModules::newDynamicModuleByName(module_name, /*do_not_close=*/true);
  if (!module_or.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to load Hickory DNS dynamic module: ", module_or.status().message()));
  }
  config->dynamic_module_ = std::move(*module_or);

  // All symbols are guaranteed to be present in the statically linked production module; any
  // failure here indicates a build configuration error rather than a user-recoverable issue.
#define RESOLVE_SYMBOL(field, symbol)                                                              \
  {                                                                                                \
    auto fn = config->dynamic_module_->getFunctionPointer<decltype(config->field)>(symbol);        \
    if (!fn.ok()) {                                                                                \
      return absl::InternalError(absl::StrCat("Failed to resolve Hickory DNS ABI symbol '",        \
                                              (symbol), "': ", fn.status().message()));            \
    }                                                                                              \
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

  auto config_json_or = serializeConfigToJson(proto_config);
  RETURN_IF_NOT_OK_REF(config_json_or.status());
  const std::string config_json = std::move(*config_json_or);

  envoy_dynamic_module_type_envoy_buffer name_buf;
  name_buf.ptr = module_name.data();
  name_buf.length = module_name.size();

  envoy_dynamic_module_type_envoy_buffer config_buf;
  config_buf.ptr = config_json.c_str();
  config_buf.length = config_json.size();

  config->in_module_config_ = config->on_dns_resolver_config_new_(
      static_cast<envoy_dynamic_module_type_dns_resolver_config_envoy_ptr>(config.get()), name_buf,
      config_buf);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Hickory DNS module rejected the configuration: invalid JSON or unsupported options.");
  }

  return config;
}

HickoryDnsResolverConfig::~HickoryDnsResolverConfig() {
  // Guard against partially-constructed instances unwound from a failed `create()`.
  // The dynamic module may not have loaded (`on_dns_resolver_config_destroy_` would be
  // null) or the Rust module may have rejected the configuration before `in_module_config_`
  // was assigned. Either case must not call through the destroy FFI.
  if (in_module_config_ != nullptr && on_dns_resolver_config_destroy_ != nullptr) {
    on_dns_resolver_config_destroy_(in_module_config_);
  }
}

// -- HickoryPendingResolution -------------------------------------------------

HickoryPendingResolution::HickoryPendingResolution(HickoryDnsResolver& parent,
                                                   DnsResolver::ResolveCb callback,
                                                   uint64_t query_id, const std::string& dns_name)
    : callback_(std::move(callback)), query_id_(query_id), dns_name_(dns_name), parent_(parent) {}

void HickoryPendingResolution::cancel(CancelReason) {
  ASSERT(parent_.dispatcher_.isThreadSafe());

  // Drop the Rust-side query state. The Rust cancel implementation flips the per-query
  // `AtomicBool` so any in-flight Tokio task observes the cancellation and skips invoking
  // the resolve-complete callback. The resolver pointer is unused by the cancel FFI; only
  // the query pointer is consumed.
  parent_.config_->on_dns_resolve_cancel_(parent_.resolver_module_ptr_, query_module_ptr_);
  query_module_ptr_ = nullptr;

  // Reconcile shell state synchronously so the gauge tracks reality and the
  // `HickoryPendingResolution` object is not leaked. Per the `ActiveDnsQuery::cancel()`
  // contract, callers must treat the pointer as invalidated after this returns. A late
  // `onResolveComplete` for this `query_id` will find no map entry and safely no-op.
  parent_.stats_.pending_resolutions_.dec();
  parent_.pending_queries_.erase(query_id_);
  delete this;
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
  // The Rust SDK only returns null if a panic propagates across the FFI boundary. Use
  // `RELEASE_ASSERT` so production builds fail loudly instead of silently dereferencing
  // a null pointer in subsequent FFI calls.
  RELEASE_ASSERT(resolver_module_ptr_ != nullptr,
                 "Hickory DNS module returned null from on_dns_resolver_new "
                 "(likely a Rust panic during resolver creation).");
}

HickoryDnsResolver::~HickoryDnsResolver() {
  // Step 1: Set the C++ shutdown flag so the ABI callback called from Tokio threads
  // skips posting to the dispatcher. The posted lambda also locks a `weak_ptr` to this
  // resolver as the final use-after-free guard.
  shutting_down_.store(true, std::memory_order_release);

  // Step 2: Destroy the module resolver. The Rust `Drop` impl signals its own
  // shutting-down flag and then performs `runtime.shutdown_timeout(5s)`, waiting up to
  // five seconds for Tokio tasks to finish; any tasks still running when the timeout
  // expires are dropped.
  config_->on_dns_resolver_destroy_(resolver_module_ptr_);

  // Step 3: Free Rust-side query objects for all remaining pending queries and delete
  // the C++ pending resolution objects. Cancelled queries are not present here: they
  // were removed and freed by `HickoryPendingResolution::cancel()`. Decrement the
  // `pending_resolutions` gauge for each remaining query since their callbacks will
  // never arrive.
  for (auto& [id, pending] : pending_queries_) {
    stats_.pending_resolutions_.dec();
    // Safe: the Rust cancel FFI ignores the resolver pointer; only the query box is
    // consumed. See `envoy_dynamic_module_on_dns_resolve_cancel` in the Rust SDK.
    config_->on_dns_resolve_cancel_(resolver_module_ptr_, pending->query_module_ptr_);
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

  const uint64_t query_id = next_query_id_++;

  envoy_dynamic_module_type_envoy_buffer name_buf;
  name_buf.ptr = dns_name.c_str();
  name_buf.length = dns_name.size();

  auto* query_module_ptr = config_->on_dns_resolve_(resolver_module_ptr_, name_buf,
                                                    toLookupFamily(dns_lookup_family), query_id);
  if (query_module_ptr == nullptr) {
    // The Rust SDK returns null when `DnsResolverInstance::resolve` returns `None` or when
    // a panic propagates across the FFI boundary. Surface the failure synchronously rather
    // than leaving the caller with no active query handle. The `pending_resolutions` gauge
    // is not incremented because no query was actually spawned; `resolve_total` and
    // `get_addr_failure` are bumped to match the asynchronous failure accounting in
    // `onResolveComplete()`.
    ENVOY_LOG(error, "Hickory DNS module failed to schedule resolution for [{}]", dns_name);
    stats_.resolve_total_.inc();
    stats_.get_addr_failure_.inc();
    callback(ResolutionStatus::Failure, "hickory_dns_dispatch_failure", {});
    return nullptr;
  }

  stats_.pending_resolutions_.inc();
  auto* pending = new HickoryPendingResolution(*this, std::move(callback), query_id, dns_name);
  pending->query_module_ptr_ = query_module_ptr;
  pending_queries_[query_id] = pending;
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

  // Free the Rust-side query object now that its task has produced a result. The cancel
  // ABI function takes ownership of the boxed query and drops it.
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
  ASSERT(dispatcher.isThreadSafe());
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig proto_config;
  RETURN_IF_NOT_OK(Envoy::MessageUtil::unpackTo(typed_config.typed_config(), proto_config));

  auto config_or = HickoryDnsResolverConfig::create(proto_config);
  RETURN_IF_NOT_OK_REF(config_or.status());

  return std::make_shared<HickoryDnsResolver>(std::move(*config_or), dispatcher, api.rootScope());
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

  // Fast path: if the resolver is already shutting down, skip the response copy entirely.
  // The Rust task is expected to observe the Rust-side shutdown flag and bail out before
  // reaching this callback, but this guard also covers the narrow window where the C++
  // destructor has set the flag while the callback is in flight on a Tokio thread.
  if (resolver->shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  // The resolver storage is kept alive across this FFI call because
  // `on_dns_resolver_destroy_` (invoked from the C++ destructor) blocks on
  // `runtime.shutdown_timeout(5s)` until in-flight FFI calls return. Capture a
  // `weak_ptr` so the posted lambda does not extend the resolver's lifetime; if the
  // dispatcher fails to drain the lambda before the resolver is destroyed, the
  // lambda's `lock()` returns `nullptr` and the callback is safely skipped.
  std::weak_ptr<Envoy::Network::HickoryDnsResolver> weak_resolver = resolver->weak_from_this();

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

  resolver->dispatcher_.post([weak_resolver = std::move(weak_resolver), query_id, status,
                              details = std::move(details_str),
                              response = std::move(response)]() mutable {
    // If the resolver was destroyed between `post()` and now, `lock()` returns `nullptr`
    // and the lambda exits safely without touching freed memory.
    if (auto resolver_shared = weak_resolver.lock()) {
      resolver_shared->onResolveComplete(query_id, status, details, std::move(response));
    }
  });
}
