#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/config.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/router/string_accessor.h"

#include "source/common/common/statusor.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tls/default_tls_certificate_selector.h"
#include "source/common/tls/server_context_impl.h"
#include "source/server/generic_factory_context.h"

#include "openssl/ssl.h"

namespace {

// Convenience: downcast opaque Envoy pointer to the concrete selector instance.
using ::Envoy::Extensions::TransportSockets::Tls::CertificateSelectors::
    DynamicModulesCertSelector::DynamicModuleCertSelector;
inline DynamicModuleCertSelector* toSelector(
    envoy_dynamic_module_type_cert_selector_envoy_ptr p) {
  return static_cast<DynamicModuleCertSelector*>(p);
}

} // namespace

// ABI callbacks. Linker-level overrides of the weak stubs in abi_impl.cc; live in the top-level
// namespace with C linkage. Each is valid only during an in-flight select() call and requires the
// selector's request-scoped state to be populated.
extern "C" {

bool envoy_dynamic_module_callback_cert_selector_get_sni(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* out) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr || selector->currentClientHello() == nullptr || out == nullptr) {
    return false;
  }
  const char* sni =
      SSL_get_servername(selector->currentClientHello()->ssl, TLSEXT_NAMETYPE_host_name);
  if (sni == nullptr) {
    out->ptr = nullptr;
    out->length = 0;
    return false;
  }
  out->ptr = sni;
  out->length = std::strlen(sni);
  return true;
}

bool envoy_dynamic_module_callback_cert_selector_get_client_hello_raw(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* out) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr || selector->currentClientHello() == nullptr || out == nullptr) {
    return false;
  }
  const SSL_CLIENT_HELLO* ch = selector->currentClientHello();
  if (ch->client_hello == nullptr || ch->client_hello_len == 0) {
    out->ptr = nullptr;
    out->length = 0;
    return false;
  }
  out->ptr = reinterpret_cast<const char*>(ch->client_hello);
  out->length = ch->client_hello_len;
  return true;
}

bool envoy_dynamic_module_callback_cert_selector_get_client_hello_extension(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr, uint16_t extension_type,
    envoy_dynamic_module_type_envoy_buffer* out) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr || selector->currentClientHello() == nullptr || out == nullptr) {
    return false;
  }
  const uint8_t* data = nullptr;
  size_t len = 0;
  if (SSL_early_callback_ctx_extension_get(selector->currentClientHello(), extension_type, &data,
                                           &len) != 1) {
    out->ptr = nullptr;
    out->length = 0;
    return false;
  }
  out->ptr = reinterpret_cast<const char*>(data);
  out->length = len;
  return true;
}

bool envoy_dynamic_module_callback_cert_selector_is_client_ocsp_capable(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr) {
    return false;
  }
  return selector->isClientOcspCapable();
}

bool envoy_dynamic_module_callback_cert_selector_set_filter_state(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr || selector->currentCallbacks() == nullptr || key.ptr == nullptr ||
      value.ptr == nullptr) {
    return false;
  }
  std::string key_str(key.ptr, key.length);
  std::string value_str(value.ptr, value.length);
  selector->currentCallbacks()->connection().streamInfo().filterState()->setData(
      key_str, std::make_shared<Envoy::Router::StringAccessorImpl>(value_str),
      Envoy::StreamInfo::FilterState::StateType::ReadOnly,
      Envoy::StreamInfo::FilterState::LifeSpan::Connection);
  return true;
}

bool envoy_dynamic_module_callback_cert_selector_get_filter_state(
    envoy_dynamic_module_type_cert_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* selector = toSelector(selector_envoy_ptr);
  if (selector == nullptr || selector->currentCallbacks() == nullptr || key.ptr == nullptr ||
      value_out == nullptr) {
    if (value_out != nullptr) {
      value_out->ptr = nullptr;
      value_out->length = 0;
    }
    return false;
  }
  std::string key_str(key.ptr, key.length);
  const auto* accessor = selector->currentCallbacks()
                             ->connection()
                             .streamInfo()
                             .filterState()
                             ->getDataReadOnly<Envoy::Router::StringAccessor>(key_str);
  if (accessor == nullptr) {
    value_out->ptr = nullptr;
    value_out->length = 0;
    return false;
  }
  absl::string_view value = accessor->asString();
  value_out->ptr = const_cast<char*>(value.data());
  value_out->length = value.size();
  return true;
}

} // extern "C"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModulesCertSelector {

// Default upper bound on the per-worker LRU when the operator leaves
// ``max_cache_size`` at 0. Picked to be roomy enough for typical
// per-tenant sets without giving an unbounded module a way to OOM workers.
constexpr uint32_t kDefaultMaxCacheSize = 1024;

DynamicModuleCertSelectorConfig::DynamicModuleCertSelectorConfig(
    std::string selector_name, std::string selector_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module)
    : selector_name_(std::move(selector_name)), selector_config_(std::move(selector_config)),
      dynamic_module_(std::move(module)) {}

DynamicModuleCertSelectorConfig::~DynamicModuleCertSelectorConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleCertSelectorConfigSharedPtr>
newDynamicModuleCertSelectorConfig(const std::string& selector_name,
                                   const std::string& selector_config,
                                   Envoy::Extensions::DynamicModules::DynamicModulePtr module) {
  auto on_config_new = module->getFunctionPointer<OnCertSelectorConfigNewType>(
      "envoy_dynamic_module_on_cert_selector_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = module->getFunctionPointer<OnCertSelectorConfigDestroyType>(
      "envoy_dynamic_module_on_cert_selector_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_new = module->getFunctionPointer<OnCertSelectorNewType>(
      "envoy_dynamic_module_on_cert_selector_new");
  RETURN_IF_NOT_OK_REF(on_new.status());

  auto on_destroy = module->getFunctionPointer<OnCertSelectorDestroyType>(
      "envoy_dynamic_module_on_cert_selector_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  auto on_select = module->getFunctionPointer<OnCertSelectorSelectType>(
      "envoy_dynamic_module_on_cert_selector_select");
  RETURN_IF_NOT_OK_REF(on_select.status());

  // Optional: config_updated.
  OnCertSelectorConfigUpdatedType on_config_updated = nullptr;
  auto maybe_on_config_updated = module->getFunctionPointer<OnCertSelectorConfigUpdatedType>(
      "envoy_dynamic_module_on_cert_selector_config_updated");
  if (maybe_on_config_updated.ok()) {
    on_config_updated = maybe_on_config_updated.value();
  }

  auto config = std::make_shared<DynamicModuleCertSelectorConfig>(selector_name, selector_config,
                                                                  std::move(module));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_new_ = on_new.value();
  config->on_destroy_ = on_destroy.value();
  config->on_select_ = on_select.value();
  config->on_config_updated_ = on_config_updated;

  envoy_dynamic_module_type_envoy_buffer name_buffer = {selector_name.data(), selector_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {selector_config.data(),
                                                          selector_config.size()};
  config->in_module_config_ =
      on_config_new.value()(static_cast<void*>(config.get()), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module cert selector config");
  }
  return config;
}

DynamicModuleCertSelector::DynamicModuleCertSelector(
    DynamicModuleCertSelectorConfigSharedPtr config,
    Ssl::TlsCertificateSelectorContext& selector_ctx,
    ThreadLocal::TypedSlot<ThreadLocalCerts>& tls_cache, Stats::Scope& stats_scope,
    Server::Configuration::ServerFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool provides_certificates,
    uint32_t max_cache_size)
    : config_(std::move(config)), tls_contexts_(selector_ctx.getTlsContexts()),
      tls_cache_(tls_cache), stats_scope_(stats_scope), factory_context_(factory_context),
      tls_config_(tls_config), provides_certificates_(provides_certificates),
      max_cache_size_(max_cache_size) {
  in_module_selector_ =
      config_->on_new_(config_->in_module_config_, static_cast<void*>(this), tls_contexts_.size());
}

DynamicModuleCertSelector::~DynamicModuleCertSelector() {
  if (config_ != nullptr && config_->on_destroy_ != nullptr) {
    config_->on_destroy_(in_module_selector_);
  }
}

DynamicContextConstSharedPtr
DynamicModuleCertSelector::getOrCreateContext(absl::string_view cache_key,
                                              absl::string_view cert_pem, absl::string_view key_pem,
                                              absl::Status& status) {
  // Per-worker cache lookup.
  ThreadLocalCerts* certs = nullptr;
  auto tls_slot = tls_cache_.get();
  if (tls_slot.has_value()) {
    certs = &(*tls_slot);
  }
  if (!cache_key.empty() && certs != nullptr) {
    auto it = certs->entries_.find(cache_key);
    if (it != certs->entries_.end()) {
      // O(1) LRU touch: splice the key's list node to the front.
      certs->lru_.splice(certs->lru_.begin(), certs->lru_, it->second.lru_it);
      return it->second.ctx;
    }
  }

  // Build a TlsCertificate proto carrying the inline PEM.
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate tls_cert;
  tls_cert.mutable_certificate_chain()->set_inline_string(std::string(cert_pem));
  tls_cert.mutable_private_key()->set_inline_string(std::string(key_pem));

  // TlsCertificateConfigImpl::create needs a TransportSocketFactoryContext (alias of
  // GenericFactoryContext). Build a local one on top of the worker's server factory context.
  Server::GenericFactoryContextImpl generic_ctx(factory_context_,
                                                factory_context_.messageValidationVisitor());
  auto cert_config_or = Ssl::TlsCertificateConfigImpl::create(
      tls_cert, generic_ctx, factory_context_.api(), config_->selectorName());
  if (!cert_config_or.ok()) {
    status = cert_config_or.status();
    return nullptr;
  }

  absl::Status creation_status = absl::OkStatus();
  auto ctx = std::make_shared<DynamicContext>(stats_scope_, factory_context_, tls_config_,
                                              *cert_config_or, creation_status);
  if (!creation_status.ok()) {
    status = creation_status;
    return nullptr;
  }

  if (!cache_key.empty() && certs != nullptr) {
    const std::string key(cache_key);
    certs->lru_.push_front(key);
    certs->entries_.emplace(key, ThreadLocalCerts::Entry{ctx, certs->lru_.begin()});
    // LRU eviction. ``max_cache_size_`` is always > 0 — the factory translates
    // an unset (0) cache_size into the kDefaultMaxCacheSize default.
    while (certs->lru_.size() > max_cache_size_) {
      certs->entries_.erase(certs->lru_.back());
      certs->lru_.pop_back();
    }
  }
  return ctx;
}

Ssl::SelectionResult
DynamicModuleCertSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                            Ssl::CertificateSelectionCallbackPtr /*cb*/) {
  // ``cb`` is intentionally dropped: this revision of the ABI is synchronous-only and
  // never returns SelectionStatus::Pending. The unique_ptr is released as the parameter
  // goes out of scope.
  current_ch_ = &ssl_client_hello;
  // ``Tls::isClientOcspCapable`` must be qualified — DynamicModuleCertSelector defines a
  // member ``isClientOcspCapable()`` that would otherwise hide the free function during
  // unqualified name lookup (and ADL is suppressed when a class member is found).
  client_ocsp_capable_ = Tls::isClientOcspCapable(ssl_client_hello);
  current_callbacks_ = static_cast<Network::TransportSocketCallbacks*>(
      SSL_get_ex_data(ssl_client_hello.ssl, ContextImpl::sslSocketIndex()));

  auto result = config_->on_select_(static_cast<void*>(this), in_module_selector_);

  // Clear request-scoped state before examining the result so callbacks cannot be invoked
  // with stale pointers.
  current_ch_ = nullptr;
  const bool was_ocsp_capable = client_ocsp_capable_;
  client_ocsp_capable_ = false;
  current_callbacks_ = nullptr;

  switch (result.status) {
  case envoy_dynamic_module_type_cert_selector_result_SuccessWithContextIndex: {
    if (result.context_index >= tls_contexts_.size()) {
      ENVOY_LOG(debug, "dynamic module cert selector returned out-of-range context index {} "
                       "(have {} contexts)",
                result.context_index, tls_contexts_.size());
      return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
    }
    const Ssl::TlsContext* ctx = &tls_contexts_[result.context_index];
    const auto staple_action =
        ocspStapleAction(*ctx, was_ocsp_capable, tls_config_.ocspStaplePolicy());
    if (staple_action == Ssl::OcspStapleAction::Fail) {
      return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
    }
    return {Ssl::SelectionResult::SelectionStatus::Success, ctx,
            staple_action == Ssl::OcspStapleAction::Staple};
  }
  case envoy_dynamic_module_type_cert_selector_result_SuccessWithPem: {
    if (!provides_certificates_) {
      ENVOY_LOG(
          debug,
          "dynamic module cert selector returned PEM but provides_certificates is disabled");
      return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
    }
    const absl::string_view cert_pem{result.cert_chain_pem.ptr, result.cert_chain_pem.length};
    const absl::string_view key_pem{result.private_key_pem.ptr, result.private_key_pem.length};
    const absl::string_view cache_key{result.cache_key.ptr, result.cache_key.length};
    if (cert_pem.empty() || key_pem.empty()) {
      ENVOY_LOG(debug, "dynamic module cert selector returned empty PEM buffers");
      return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
    }
    absl::Status ctx_status = absl::OkStatus();
    auto dyn_ctx = getOrCreateContext(cache_key, cert_pem, key_pem, ctx_status);
    if (!ctx_status.ok() || dyn_ctx == nullptr) {
      ENVOY_LOG(debug, "failed to compile dynamic cert context: {}", ctx_status.message());
      return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
    }
    const Ssl::TlsContext& compiled = dyn_ctx->tlsContext();
    const bool staple = result.staple_ocsp && was_ocsp_capable;
    return {Ssl::SelectionResult::SelectionStatus::Success, &compiled, staple,
            std::make_shared<Handle>(std::move(dyn_ctx))};
  }
  case envoy_dynamic_module_type_cert_selector_result_Failed:
  default:
    return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
  }
}

DynamicModuleCertSelectorFactory::DynamicModuleCertSelectorFactory(
    DynamicModuleCertSelectorConfigSharedPtr config,
    std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalCerts>> cache,
    Stats::ScopeSharedPtr stats_scope,
    Server::Configuration::ServerFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool provides_certificates,
    uint32_t max_cache_size)
    : config_(std::move(config)), tls_cache_(std::move(cache)), stats_scope_(std::move(stats_scope)),
      factory_context_(factory_context), tls_config_(tls_config),
      provides_certificates_(provides_certificates), max_cache_size_(max_cache_size) {}

Ssl::TlsCertificateSelectorPtr
DynamicModuleCertSelectorFactory::create(Ssl::TlsCertificateSelectorContext& selector_ctx) {
  return std::make_unique<DynamicModuleCertSelector>(config_, selector_ctx, *tls_cache_,
                                                     *stats_scope_, factory_context_, tls_config_,
                                                     provides_certificates_, max_cache_size_);
}

absl::Status DynamicModuleCertSelectorFactory::onConfigUpdate() {
  if (config_->on_config_updated_ != nullptr) {
    config_->on_config_updated_(config_->in_module_config_);
  }
  // Flush the per-worker compiled-context cache — the parent TLS config may have rotated
  // pre-provisioned contexts, and any cached dynamic context can outlive its intended config.
  tls_cache_->runOnAllThreads([](OptRef<ThreadLocalCerts> certs) {
    if (certs.has_value()) {
      certs->entries_.clear();
      certs->lru_.clear();
    }
  });
  return absl::OkStatus();
}

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
DynamicModuleCertSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError(
        "envoy.tls.certificate_selectors.dynamic_modules does not support QUIC listeners");
  }

  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());

  // If the module is going to provide its own certificate material, session resumption must be
  // disabled on the parent context — the session ID is keyed on the pre-provisioned certs.
  if (config.provides_certificates() &&
      (!tls_config.disableStatelessSessionResumption() ||
       !tls_config.disableStatefulSessionResumption())) {
    return absl::InvalidArgumentError(
        "Dynamic module cert selectors with provides_certificates=true require session "
        "resumption to be disabled on the parent TLS context.");
  }

  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      config.dynamic_module_config().name(), config.dynamic_module_config().do_not_close(),
      config.dynamic_module_config().load_globally());
  if (!module_or_error.ok()) {
    return module_or_error.status();
  }

  std::string selector_config_bytes;
  if (config.has_selector_config()) {
    auto bytes_or_error = MessageUtil::knownAnyToBytes(config.selector_config());
    RETURN_IF_NOT_OK_REF(bytes_or_error.status());
    selector_config_bytes = std::move(bytes_or_error.value());
  }

  auto module_config_or_error = newDynamicModuleCertSelectorConfig(
      config.selector_name(), selector_config_bytes, std::move(module_or_error.value()));
  if (!module_config_or_error.ok()) {
    return module_config_or_error.status();
  }

  // Per-worker cache slot.
  auto cache = std::make_unique<ThreadLocal::TypedSlot<ThreadLocalCerts>>(
      factory_context.serverFactoryContext().threadLocal());
  cache->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });

  auto stats_scope = factory_context.scope().createScope("cert_selector.dynamic_modules.");

  const uint32_t cache_size =
      config.max_cache_size() == 0 ? kDefaultMaxCacheSize : config.max_cache_size();
  return std::make_unique<DynamicModuleCertSelectorFactory>(
      std::move(module_config_or_error.value()), std::move(cache), std::move(stats_scope),
      factory_context.serverFactoryContext(), tls_config, config.provides_certificates(),
      cache_size);
}

REGISTER_FACTORY(DynamicModuleCertSelectorConfigFactory, Ssl::TlsCertificateSelectorConfigFactory);

} // namespace DynamicModulesCertSelector
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
