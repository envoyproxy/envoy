#include "source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/tls_certificate_selector.h"

#include <algorithm>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/init/manager.h"

#include "source/common/common/thread.h"
#include "source/common/tls/default_tls_certificate_selector.h"
#include "source/common/tls/server_context_impl.h"
#include "source/common/tls/utility.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

namespace {

// Serves the TLS context with its OCSP response when the stapling policy staples it, or fails the
// handshake when the policy fails it.
Ssl::SelectionResult serveTlsContext(const Ssl::TlsContext& tls_context, bool client_ocsp_capable,
                                     Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy,
                                     Ssl::SelectionHandleConstSharedPtr handle = nullptr) {
  const Ssl::OcspStapleAction ocsp_staple_action =
      ocspStapleAction(tls_context, client_ocsp_capable, ocsp_staple_policy);
  if (ocsp_staple_action == Ssl::OcspStapleAction::Fail) {
    ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::dynamic_modules), debug,
                        "TLS certificate selected by the dynamic module fails the OCSP policy");
    return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
  }
  return {.status = Ssl::SelectionResult::SelectionStatus::Success,
          .selected_ctx = &tls_context,
          .staple = ocsp_staple_action == Ssl::OcspStapleAction::Staple,
          .handle = std::move(handle)};
}

// Until SDS delivers a configured validation context, a TLS context built from the configuration
// would not verify peers.
bool validationContextReady(const Ssl::ContextConfig& tls_config) {
  return !tls_config.validationContextConfigured() ||
         tls_config.certificateValidationContext() != nullptr;
}

// Reports a dynamic certificate to the context manager without the trusted certificates, which the
// TLS context of the selector reports once for all of its dynamic certificates.
class DynamicCertificateView : public Ssl::Context {
public:
  DynamicCertificateView(AsyncContextConstSharedPtr context, TimeSource& time_source)
      : context_(std::move(context)),
        ssl_context_(dynamic_cast<const Ssl::Context*>(context_.get())), time_source_(time_source) {
    ASSERT(ssl_context_ != nullptr);
  }

  // Ssl::Context
  std::optional<uint32_t> daysUntilFirstCertExpires() const override {
    return Utility::getDaysUntilExpiration(context_->tlsContext().cert_chain_.get(), time_source_);
  }
  std::vector<Ssl::CertificateDetailsPtr> getCaCertInformation() const override { return {}; }
  std::vector<Ssl::CertificateDetailsPtr> getCertChainInformation() const override {
    return ssl_context_->getCertChainInformation();
  }
  std::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override {
    return ssl_context_->secondsUntilFirstOcspResponseExpires();
  }

private:
  const AsyncContextConstSharedPtr context_;
  const Ssl::Context* const ssl_context_;
  TimeSource& time_source_;
};

} // namespace

void DynamicCertificateHandle::notify(AsyncContextConstSharedPtr context) {
  Ssl::SelectionResult result{.status = Ssl::SelectionResult::SelectionStatus::Failed};
  if (context != nullptr) {
    result =
        serveTlsContext(context->tlsContext(), client_ocsp_capable_, context->ocspStaplePolicy());
    context_ = context;
  }
  Event::Dispatcher& dispatcher = cb_->dispatcher();
  // The context capture keeps the selected TLS context alive until the callback runs.
  dispatcher.post([cb = std::move(cb_), context = std::move(context),
                   selected_ctx = result.selected_ctx, staple = result.staple] {
    cb->onCertificateSelectionResult(makeOptRefFromPtr(selected_ctx), staple);
  });
}

DynamicModuleTlsCertificateSelectorConfig::DynamicModuleTlsCertificateSelectorConfig(
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ContextConfig& tls_config, absl::string_view metrics_namespace,
    std::chrono::milliseconds certificate_request_timeout,
    AsyncContextFactory dynamic_context_factory)
    : main_thread_dispatcher_(factory_context.serverFactoryContext().mainThreadDispatcher()),
      dynamic_module_(std::move(dynamic_module)),
      server_context_(factory_context.serverFactoryContext()),
      context_manager_(server_context_.sslContextManager()), tls_config_(&tls_config),
      certificate_request_timeout_(certificate_request_timeout),
      init_target_(
          std::make_unique<Init::TargetImpl>("dynamic_modules_tls_certificate_selector", [] {})),
      dynamic_context_factory_(std::move(dynamic_context_factory)),
      context_scope_(factory_context.scope().createScope("")),
      stats_scope_(server_context_.scope().createScope(absl::StrCat(metrics_namespace, "."))),
      metrics_(*stats_scope_) {
  factory_context.initManager().add(*init_target_);
  if (dynamic_context_factory_ != nullptr) {
    // The socket factory is created right after the configuration, and installs a TLS context built
    // from it if it is ready.
    tls_context_installed_ = tls_config.isReady();
    stats_ = std::make_unique<DynamicCertificateStats>(DynamicCertificateStats{
        ALL_DYNAMIC_CERTIFICATE_STATS(POOL_COUNTER_PREFIX(*context_scope_, "dynamic_certificates"),
                                      POOL_GAUGE_PREFIX(*context_scope_, "dynamic_certificates"))});
    thread_local_certificates_ =
        ThreadLocal::TypedSlot<ThreadLocalCertificates>::makeUnique(server_context_.threadLocal());
    thread_local_certificates_->set(
        [](Event::Dispatcher&) { return std::make_shared<ThreadLocalCertificates>(); });
  }
}

DynamicModuleTlsCertificateSelectorConfig::~DynamicModuleTlsCertificateSelectorConfig() {
  if (in_module_config_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
  // Requests still waiting for the module end with the configuration.
  if (stats_ != nullptr) {
    stats_->pending_.sub(std::count_if(
        dynamic_certificates_.begin(), dynamic_certificates_.end(),
        [](const auto& entry) { return !entry.second.certificate_config.has_value(); }));
  }
}

bool DynamicModuleTlsCertificateSelectorConfig::setCertificate(absl::string_view name,
                                                               absl::string_view certificate_chain,
                                                               absl::string_view private_key,
                                                               absl::string_view ocsp_staple) {
  // The dynamic certificates are main thread state, so reject calls from other threads.
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate "
                 "must be called on the main thread");
    return false;
  }
  if (!allowsDynamicCertificates()) {
    ENVOY_LOG(warn,
              "dynamic module set TLS certificate '{}', but dynamic certificates are not allowed",
              name);
    return false;
  }
  if (tls_config_ == nullptr) {
    ENVOY_LOG(debug, "dynamic module set TLS certificate '{}', but its TLS context is removed",
              name);
    stats_->rejected_.inc();
    failRequestedCertificate(name);
    return false;
  }
  const absl::Status status =
      addCertificate(std::string(name), certificate_chain, private_key, ocsp_staple);
  if (!status.ok()) {
    ENVOY_LOG(warn, "dynamic module set an invalid TLS certificate '{}': {}", name,
              status.message());
    stats_->rejected_.inc();
    failRequestedCertificate(name);
    return false;
  }
  stats_->provided_.inc();
  return true;
}

void DynamicModuleTlsCertificateSelectorConfig::failRequestedCertificate(absl::string_view name) {
  // A certificate that is only requested has neither a context nor a configuration.
  const auto it = dynamic_certificates_.find(name);
  if (it != dynamic_certificates_.end() && it->second.context == nullptr &&
      !it->second.certificate_config.has_value()) {
    removeCertificate(name);
  }
}

absl::Status DynamicModuleTlsCertificateSelectorConfig::addCertificate(
    const std::string& name, absl::string_view certificate_chain, absl::string_view private_key,
    absl::string_view ocsp_staple) {
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate proto;
  proto.mutable_certificate_chain()->set_inline_bytes(certificate_chain);
  proto.mutable_private_key()->set_inline_bytes(private_key);
  if (!ocsp_staple.empty()) {
    proto.mutable_ocsp_staple()->set_inline_bytes(ocsp_staple);
  }
  Server::GenericFactoryContextImpl generic_context(server_context_,
                                                    server_context_.messageValidationVisitor());
  auto certificate_config =
      Ssl::TlsCertificateConfigImpl::create(proto, generic_context, server_context_.api(), name);
  RETURN_IF_NOT_OK_REF(certificate_config.status());
  // onConfigUpdate() builds the certificate once the validation context arrives.
  if (!validationContextReady(*tls_config_)) {
    storeProvidedCertificate(name, *std::move(certificate_config));
    return absl::OkStatus();
  }
  auto context = buildContext(*certificate_config);
  RETURN_IF_NOT_OK_REF(context.status());
  DynamicCertificate& certificate = storeProvidedCertificate(name, *std::move(certificate_config));
  activateCertificate(name, certificate, *std::move(context));
  return absl::OkStatus();
}

DynamicModuleTlsCertificateSelectorConfig::DynamicCertificate&
DynamicModuleTlsCertificateSelectorConfig::storeProvidedCertificate(
    const std::string& name, Ssl::TlsCertificateConfigImpl&& certificate_config) {
  const auto [it, inserted] = dynamic_certificates_.try_emplace(name);
  DynamicCertificate& certificate = it->second;
  if (!inserted && !certificate.certificate_config.has_value()) {
    stats_->pending_.dec();
    certificate.request_timer.reset();
  }
  certificate.certificate_config.emplace(std::move(certificate_config));
  return certificate;
}

void DynamicModuleTlsCertificateSelectorConfig::removeCertificate(absl::string_view name) {
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate "
                 "must be called on the main thread");
    return;
  }
  const auto it = dynamic_certificates_.find(name);
  if (it == dynamic_certificates_.end()) {
    return;
  }
  const std::vector<std::weak_ptr<DynamicCertificateHandle>> waiting_handles =
      std::move(it->second.waiting_handles);
  if (it->second.context != nullptr) {
    updateWorkers(it->first, nullptr);
  }
  if (it->second.certificate_config.has_value()) {
    stats_->removed_.inc();
  } else {
    stats_->pending_.dec();
  }
  replaceManagedContext(it->second, nullptr);
  dynamic_certificates_.erase(it);
  for (const auto& weak_handle : waiting_handles) {
    if (const auto handle = weak_handle.lock(); handle != nullptr) {
      handle->notify(nullptr);
    }
  }
}

void DynamicModuleTlsCertificateSelectorConfig::onConfigUpdate() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // The socket factory installed a TLS context built from the updated configuration before this
  // call, or none if the configuration is not ready.
  tls_context_installed_ = tls_config_->isReady();
  if (!tls_context_installed_) {
    for (auto& [_, certificate] : dynamic_certificates_) {
      replaceManagedContext(certificate, nullptr);
    }
  }
  if (!validationContextReady(*tls_config_)) {
    return;
  }
  std::vector<std::string> invalid_names;
  for (auto& [name, certificate] : dynamic_certificates_) {
    if (!certificate.certificate_config.has_value()) {
      continue;
    }
    auto context = buildContext(*certificate.certificate_config);
    if (!context.ok()) {
      ENVOY_LOG(warn,
                "dynamic module TLS certificate '{}' is removed, since it fails with the updated "
                "TLS context: {}",
                name, context.status().message());
      invalid_names.push_back(name);
      continue;
    }
    activateCertificate(name, certificate, *std::move(context));
  }
  for (const std::string& name : invalid_names) {
    removeCertificate(name);
  }
}

void DynamicModuleTlsCertificateSelectorConfig::detachTlsConfig() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // The context manager, the timers and the init target are main thread state, and the
  // configuration may be destroyed on a worker.
  for (auto& [_, certificate] : dynamic_certificates_) {
    replaceManagedContext(certificate, nullptr);
    certificate.request_timer.reset();
  }
  init_target_.reset();
  tls_config_ = nullptr;
  dynamic_context_factory_ = nullptr;
}

void DynamicModuleTlsCertificateSelectorConfig::signalInitComplete() {
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_tls_certificate_selector_config_signal_init_complete must "
        "be called on the main thread");
    return;
  }
  if (init_target_ == nullptr) {
    return;
  }
  // A selector hook can signal while a TLS context of the listener is built, and finishing the
  // warming of the listener can destroy it, so the target is marked ready in a separate event.
  main_thread_dispatcher_.post([weak_config = weak_from_this()] {
    if (const auto config = weak_config.lock();
        config != nullptr && config->init_target_ != nullptr) {
      config->init_target_->ready();
      config->init_target_.reset();
    }
  });
}

Ssl::SelectionResult DynamicModuleTlsCertificateSelectorConfig::selectDynamicCertificate(
    absl::string_view name, bool client_ocsp_capable, Ssl::CertificateSelectionCallbackPtr&& cb) {
  if (!allowsDynamicCertificates()) {
    ENVOY_LOG(debug,
              "dynamic module selected TLS certificate '{}', but dynamic certificates are not "
              "allowed",
              name);
    return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
  }
  const auto& contexts = thread_local_certificates_->get()->contexts;
  if (const auto it = contexts.find(name); it != contexts.end()) {
    // The handle keeps the context alive for the connection, since its callbacks use the context.
    return serveTlsContext(it->second->tlsContext(), client_ocsp_capable,
                           it->second->ocspStaplePolicy(),
                           std::make_shared<DynamicCertificateHandle>(it->second));
  }
  // Without the requested hook the module is never asked for the certificate.
  if (on_certificate_requested_ == nullptr) {
    ENVOY_LOG(debug, "dynamic module selected TLS certificate '{}', which it has not provided",
              name);
    return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
  }
  ASSERT(cb != nullptr);
  auto handle = std::make_shared<DynamicCertificateHandle>(std::move(cb), client_ocsp_capable);
  // The configuration and the handshake can both be gone by the time the main thread runs this.
  main_thread_dispatcher_.post([weak_config = weak_from_this(), name = std::string(name),
                                weak_handle = std::weak_ptr<DynamicCertificateHandle>(handle)] {
    if (const auto config = weak_config.lock(); config != nullptr) {
      config->requestCertificate(name, weak_handle);
    }
  });
  return {.status = Ssl::SelectionResult::SelectionStatus::Pending, .handle = std::move(handle)};
}

void DynamicModuleTlsCertificateSelectorConfig::onScheduled(uint64_t event_id) {
  if (on_config_scheduled_ != nullptr) {
    on_config_scheduled_(this, in_module_config_, event_id);
  }
}

void DynamicModuleTlsCertificateSelectorConfig::requestCertificate(
    const std::string& name, const std::weak_ptr<DynamicCertificateHandle>& weak_handle) {
  const auto handle = weak_handle.lock();
  if (handle == nullptr) {
    return;
  }
  const auto [it, inserted] = dynamic_certificates_.try_emplace(name);
  DynamicCertificate& certificate = it->second;
  if (certificate.context != nullptr) {
    // The certificate was provided after the worker looked it up.
    handle->notify(certificate.context);
    return;
  }
  // Drop the handles of finished handshakes before the vector grows, which bounds its size.
  if (certificate.waiting_handles.size() == certificate.waiting_handles.capacity()) {
    std::erase_if(certificate.waiting_handles,
                  [](const std::weak_ptr<DynamicCertificateHandle>& waiting_handle) {
                    return waiting_handle.expired();
                  });
  }
  certificate.waiting_handles.push_back(handle);
  if (inserted) {
    ENVOY_LOG(debug, "requesting TLS certificate '{}' from the dynamic module", name);
    stats_->requested_.inc();
    stats_->pending_.inc();
    // No timer starts once the TLS context is removed, since the configuration may then be
    // destroyed on a worker.
    if (certificate_request_timeout_.count() > 0 && tls_config_ != nullptr) {
      certificate.request_timer =
          main_thread_dispatcher_.createTimer([this, name] { onRequestTimeout(name); });
      certificate.request_timer->enableTimer(certificate_request_timeout_);
    }
    // The hook can provide or remove the certificate, so the entry is not used after it returns.
    on_certificate_requested_(this, in_module_config_, {name.data(), name.size()});
  }
}

void DynamicModuleTlsCertificateSelectorConfig::onRequestTimeout(std::string name) {
  ENVOY_LOG(debug, "dynamic module did not answer the request for TLS certificate '{}' in time",
            name);
  stats_->request_timeout_.inc();
  failRequestedCertificate(name);
}

absl::StatusOr<AsyncContextSharedPtr> DynamicModuleTlsCertificateSelectorConfig::buildContext(
    const Ssl::TlsCertificateConfig& certificate_config) {
  absl::Status creation_status = absl::OkStatus();
  AsyncContextSharedPtr context = dynamic_context_factory_(*context_scope_, server_context_,
                                                           certificate_config, creation_status);
  RETURN_IF_NOT_OK(creation_status);
  return context;
}

void DynamicModuleTlsCertificateSelectorConfig::activateCertificate(const std::string& name,
                                                                    DynamicCertificate& certificate,
                                                                    AsyncContextSharedPtr context) {
  // Only a certificate that an installed TLS context can serve is reported. onConfigUpdate()
  // activates the certificate again once a TLS context is installed, and a handshaker that
  // provides the certificates builds the context without one.
  Ssl::ContextSharedPtr managed_context;
  if (tls_context_installed_ && context->tlsContext().cert_chain_ != nullptr) {
    managed_context =
        std::make_shared<DynamicCertificateView>(context, server_context_.timeSource());
  }
  replaceManagedContext(certificate, std::move(managed_context));
  certificate.context = context;
  updateWorkers(name, context);
  for (const auto& weak_handle : std::exchange(certificate.waiting_handles, {})) {
    if (const auto handle = weak_handle.lock(); handle != nullptr) {
      handle->notify(context);
    }
  }
}

void DynamicModuleTlsCertificateSelectorConfig::replaceManagedContext(
    DynamicCertificate& certificate, Ssl::ContextSharedPtr&& context) {
  if (certificate.managed_context != nullptr) {
    context_manager_.removeContext(certificate.managed_context);
    stats_->active_.dec();
  }
  certificate.managed_context = std::move(context);
  if (certificate.managed_context != nullptr) {
    context_manager_.addContext(certificate.managed_context);
    stats_->active_.inc();
  }
}

void DynamicModuleTlsCertificateSelectorConfig::updateWorkers(const std::string& name,
                                                              AsyncContextConstSharedPtr context) {
  // The module can still change certificates from its destroy hooks after threading shuts down.
  if (thread_local_certificates_->isShutdown()) {
    return;
  }
  thread_local_certificates_->runOnAllThreads(
      [name, context = std::move(context)](OptRef<ThreadLocalCertificates> certificates) {
        if (context != nullptr) {
          certificates->contexts[name] = context;
        } else {
          certificates->contexts.erase(name);
        }
      });
}

absl::StatusOr<DynamicModuleTlsCertificateSelectorConfigSharedPtr>
newDynamicModuleTlsCertificateSelectorConfig(
    absl::string_view selector_name, absl::string_view selector_config, bool is_upstream,
    absl::string_view metrics_namespace, std::chrono::milliseconds certificate_request_timeout,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ContextConfig& tls_config, AsyncContextFactory dynamic_context_factory) {
  auto on_config_new = dynamic_module->getFunctionPointer<OnTlsCertificateSelectorConfigNewType>(
      "envoy_dynamic_module_on_tls_certificate_selector_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnTlsCertificateSelectorConfigDestroyType>(
          "envoy_dynamic_module_on_tls_certificate_selector_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_new = dynamic_module->getFunctionPointer<OnTlsCertificateSelectorNewType>(
      "envoy_dynamic_module_on_tls_certificate_selector_new");
  RETURN_IF_NOT_OK_REF(on_new.status());

  auto on_destroy = dynamic_module->getFunctionPointer<OnTlsCertificateSelectorDestroyType>(
      "envoy_dynamic_module_on_tls_certificate_selector_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  auto on_select = dynamic_module->getFunctionPointer<OnTlsCertificateSelectorSelectType>(
      "envoy_dynamic_module_on_tls_certificate_selector_select");
  RETURN_IF_NOT_OK_REF(on_select.status());

  // Optional. Modules that do not use dynamic certificates or schedule events do not need them.
  auto on_certificate_requested =
      dynamic_module->getFunctionPointer<OnTlsCertificateSelectorConfigCertificateRequestedType>(
          "envoy_dynamic_module_on_tls_certificate_selector_config_certificate_requested");
  auto on_config_scheduled =
      dynamic_module->getFunctionPointer<OnTlsCertificateSelectorConfigScheduledType>(
          "envoy_dynamic_module_on_tls_certificate_selector_config_scheduled");

  auto config = std::make_shared<DynamicModuleTlsCertificateSelectorConfig>(
      std::move(dynamic_module), factory_context, tls_config, metrics_namespace,
      certificate_request_timeout, std::move(dynamic_context_factory));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_new_ = on_new.value();
  config->on_destroy_ = on_destroy.value();
  config->on_select_ = on_select.value();
  if (on_certificate_requested.ok()) {
    config->on_certificate_requested_ = on_certificate_requested.value();
  }
  if (on_config_scheduled.ok()) {
    config->on_config_scheduled_ = on_config_scheduled.value();
  }

  const envoy_dynamic_module_type_envoy_buffer name_buffer = {selector_name.data(),
                                                              selector_name.size()};
  const envoy_dynamic_module_type_envoy_buffer config_buffer = {selector_config.data(),
                                                                selector_config.size()};
  config->in_module_config_ = on_config_new.value()(static_cast<void*>(config.get()), name_buffer,
                                                    config_buffer, is_upstream);
  if (config->in_module_config_ == nullptr) {
    // The module may have provided dynamic certificates before it failed.
    config->detachTlsConfig();
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module TLS certificate selector config");
  }
  // Release-store pairs with the acquire-load when the module defines a metric. After this point
  // the module can only record previously defined metrics, so the registry is read-only.
  config->stat_creation_frozen_.store(true, std::memory_order_release);
  return config;
}

void DynamicModuleTlsCertificateSelectorConfigScheduler::commit(uint64_t event_id) {
  // Lock the config so its dispatcher member stays valid across post().
  const auto config_shared = config_.lock();
  if (config_shared == nullptr) {
    return;
  }
  config_shared->main_thread_dispatcher_.post([config = config_, event_id]() {
    if (const auto cs = config.lock(); cs != nullptr) {
      cs->onScheduled(event_id);
    }
  });
}

DynamicModuleTlsCertificateSelector::DynamicModuleTlsCertificateSelector(
    DynamicModuleTlsCertificateSelectorConfigSharedPtr config,
    Ssl::TlsCertificateSelectorContext& selector_context,
    Ssl::TlsCertificateSelectorPtr default_selector,
    Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy)
    : config_(std::move(config)), tls_contexts_(selector_context.getTlsContexts()),
      default_selector_(std::move(default_selector)), ocsp_staple_policy_(ocsp_staple_policy) {
  // The DER encodings only need to outlive the call, since the module copies what it keeps.
  std::vector<bssl::UniquePtr<uint8_t>> der_owners;
  std::vector<envoy_dynamic_module_type_envoy_buffer> certificates;
  der_owners.reserve(tls_contexts_.size());
  certificates.reserve(tls_contexts_.size());
  for (const Ssl::TlsContext& tls_context : tls_contexts_) {
    uint8_t* der = nullptr;
    const int der_length =
        tls_context.cert_chain_ != nullptr ? i2d_X509(tls_context.cert_chain_.get(), &der) : 0;
    der_owners.emplace_back(der);
    certificates.push_back(
        {reinterpret_cast<const char*>(der), der_length > 0 ? static_cast<size_t>(der_length) : 0});
  }
  in_module_selector_ =
      config_->on_new_(config_->in_module_config_, certificates.data(), certificates.size());
  if (in_module_selector_ == nullptr) {
    ENVOY_LOG(error, "dynamic module failed to create a TLS certificate selector, so every "
                     "handshake that uses the TLS context fails");
  }
}

DynamicModuleTlsCertificateSelector::~DynamicModuleTlsCertificateSelector() {
  if (in_module_selector_ != nullptr) {
    config_->on_destroy_(in_module_selector_);
  }
}

Ssl::SelectionResult
DynamicModuleTlsCertificateSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                      Ssl::CertificateSelectionCallbackPtr cb) {
  TlsCertificateSelectionContext context{*ssl_client_hello.ssl, &ssl_client_hello};
  return selectCertificate(context, isClientOcspCapable(ssl_client_hello), std::move(cb));
}

Ssl::SelectionResult DynamicModuleTlsCertificateSelector::selectTlsContext(
    const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr&,
    Ssl::CertificateSelectionCallbackPtr cb) {
  TlsCertificateSelectionContext context{ssl, nullptr};
  // OCSP stapling only applies to server certificates.
  return selectCertificate(context, false, std::move(cb));
}

Ssl::SelectionResult
DynamicModuleTlsCertificateSelector::selectCertificate(TlsCertificateSelectionContext& context,
                                                       bool client_ocsp_capable,
                                                       Ssl::CertificateSelectionCallbackPtr&& cb) {
  if (in_module_selector_ == nullptr) {
    return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
  }
  size_t certificate_index = 0;
  envoy_dynamic_module_type_module_buffer certificate_name = {nullptr, 0};
  switch (
      config_->on_select_(in_module_selector_, &context, &certificate_index, &certificate_name)) {
  case envoy_dynamic_module_type_tls_certificate_selector_status_PassThrough:
    if (default_selector_ != nullptr) {
      return default_selector_->selectTlsContext(*context.client_hello, std::move(cb));
    }
    // Without a custom selector, an upstream TLS context uses its first certificate.
    return serveTlsContext(tls_contexts_.front(), client_ocsp_capable, ocsp_staple_policy_);
  case envoy_dynamic_module_type_tls_certificate_selector_status_Configured:
    if (certificate_index >= tls_contexts_.size()) {
      ENVOY_LOG(debug,
                "dynamic module selected TLS certificate index {}, but the TLS context has {} "
                "certificates",
                certificate_index, tls_contexts_.size());
      return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
    }
    return serveTlsContext(tls_contexts_[certificate_index], client_ocsp_capable,
                           ocsp_staple_policy_);
  case envoy_dynamic_module_type_tls_certificate_selector_status_Dynamic:
    return config_->selectDynamicCertificate({certificate_name.ptr, certificate_name.length},
                                             client_ocsp_capable, std::move(cb));
  case envoy_dynamic_module_type_tls_certificate_selector_status_Failed:
    break;
  }
  return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
}

Ssl::TlsCertificateSelectorPtr DynamicModuleTlsCertificateSelectorFactory::create(
    Ssl::TlsCertificateSelectorContext& selector_context) {
  return std::make_unique<DynamicModuleTlsCertificateSelector>(
      config_, selector_context,
      std::make_unique<DefaultTlsCertificateSelector>(server_config_, selector_context),
      server_config_.ocspStaplePolicy());
}

Ssl::UpstreamTlsCertificateSelectorPtr
DynamicModuleUpstreamTlsCertificateSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext& selector_context) {
  // Upstream TLS contexts never staple OCSP responses, so the stapling policy is not consulted.
  return std::make_unique<DynamicModuleTlsCertificateSelector>(
      config_, selector_context, nullptr,
      Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling);
}

} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
