#include "source/extensions/common/wasm/wasm.h"

#include <algorithm>
#include <chrono>

#include "envoy/event/deferred_deletable.h"

#include "source/common/common/logger.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/extensions/common/wasm/plugin.h"
#include "source/extensions/common/wasm/stats_handler.h"

#include "absl/strings/str_cat.h"

using proxy_wasm::FailState;
using proxy_wasm::Word;

namespace Envoy {

using ScopeWeakPtr = std::weak_ptr<Stats::Scope>;

namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

struct CodeCacheEntry {
  std::string code;
  bool in_progress;
  MonotonicTime use_time;
  MonotonicTime fetch_time;
};

class RemoteDataFetcherAdapter : public Config::DataFetcher::RemoteDataFetcherCallback,
                                 public Event::DeferredDeletable {
public:
  RemoteDataFetcherAdapter(std::function<void(std::string cb)> cb) : cb_(cb) {}
  ~RemoteDataFetcherAdapter() override = default;
  void onSuccess(const std::string& data) override { cb_(data); }
  void onFailure(Config::DataFetcher::FailureReason) override { cb_(""); }
  void setFetcher(std::unique_ptr<Config::DataFetcher::RemoteDataFetcher>&& fetcher) {
    fetcher_ = std::move(fetcher);
  }

private:
  std::function<void(std::string)> cb_;
  std::unique_ptr<Config::DataFetcher::RemoteDataFetcher> fetcher_;
};

const std::string INLINE_STRING = "<inline>";
const int CODE_CACHE_SECONDS_NEGATIVE_CACHING = 10;
const int CODE_CACHE_SECONDS_CACHING_TTL = 24 * 3600; // 24 hours.
MonotonicTime::duration cache_time_offset_for_testing{};

std::mutex code_cache_mutex;
absl::flat_hash_map<std::string, CodeCacheEntry>* code_cache = nullptr;

// Downcast WasmBase to the actual Wasm.
inline Wasm* getWasm(WasmHandleSharedPtr& base_wasm_handle) {
  return static_cast<Wasm*>(base_wasm_handle->wasm().get());
}

} // namespace

void Wasm::initializeLifecycle(Server::ServerLifecycleNotifier& lifecycle_notifier) {
  auto weak = std::weak_ptr<Wasm>(std::static_pointer_cast<Wasm>(shared_from_this()));
  lifecycle_notifier.registerCallback(Server::ServerLifecycleNotifier::Stage::ShutdownExit,
                                      [this, weak](Event::PostCb post_cb) {
                                        auto lock = weak.lock();
                                        if (lock) { // See if we are still alive.
                                          server_shutdown_post_cb_ = std::move(post_cb);
                                        }
                                      });
}

Wasm::Wasm(WasmConfig& config, absl::string_view vm_key, const Stats::ScopeSharedPtr& scope,
           Api::Api& api, Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher)
    : WasmBase(
          createWasmVm(config.config().vm_config().runtime()), config.config().vm_config().vm_id(),
          MessageUtil::anyToBytes(config.config().vm_config().configuration()),
          toStdStringView(vm_key), config.environmentVariables(), config.allowedCapabilities()),
      scope_(scope), api_(api), stat_name_pool_(scope_->symbolTable()),
      custom_stat_namespace_(stat_name_pool_.add(CustomStatNamespace)),
      cluster_manager_(cluster_manager), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()), lifecycle_stats_handler_(LifecycleStatsHandler(
                                                 scope, config.config().vm_config().runtime())) {
  lifecycle_stats_handler_.onEvent(WasmEvent::VmCreated);
  ENVOY_LOG(debug, "Base Wasm created {} now active", lifecycle_stats_handler_.getActiveVmCount());
}

Wasm::Wasm(WasmHandleSharedPtr base_wasm_handle, Event::Dispatcher& dispatcher)
    : WasmBase(base_wasm_handle,
               [&base_wasm_handle]() {
                 return createWasmVm(absl::StrCat(
                     "envoy.wasm.runtime.",
                     toAbslStringView(base_wasm_handle->wasm()->wasm_vm()->getEngineName())));
               }),
      scope_(getWasm(base_wasm_handle)->scope_), api_(getWasm(base_wasm_handle)->api_),
      stat_name_pool_(scope_->symbolTable()),
      custom_stat_namespace_(stat_name_pool_.add(CustomStatNamespace)),
      cluster_manager_(getWasm(base_wasm_handle)->clusterManager()), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()),
      lifecycle_stats_handler_(getWasm(base_wasm_handle)->lifecycle_stats_handler_) {
  lifecycle_stats_handler_.onEvent(WasmEvent::VmCreated);
  ENVOY_LOG(debug, "Thread-Local Wasm created {} now active",
            lifecycle_stats_handler_.getActiveVmCount());
}

void Wasm::error(std::string_view message) { ENVOY_LOG(error, "Wasm VM failed {}", message); }

void Wasm::setTimerPeriod(uint32_t context_id, std::chrono::milliseconds new_period) {
  auto& period = timer_period_[context_id];
  auto& timer = timer_[context_id];
  bool was_running = timer && period.count() > 0;
  period = new_period;
  if (was_running) {
    timer->disableTimer();
  }
  if (period.count() > 0) {
    timer = dispatcher_.createTimer(
        [weak = std::weak_ptr<Wasm>(std::static_pointer_cast<Wasm>(shared_from_this())),
         context_id]() {
          auto shared = weak.lock();
          if (shared) {
            shared->tickHandler(context_id);
          }
        });
    timer->enableTimer(period);
  }
}

void Wasm::tickHandler(uint32_t root_context_id) {
  auto period = timer_period_.find(root_context_id);
  auto timer = timer_.find(root_context_id);
  if (period == timer_period_.end() || timer == timer_.end() || !on_tick_) {
    return;
  }
  auto context = getContext(root_context_id);
  if (context) {
    context->onTick(0);
  }
  if (timer->second && period->second.count() > 0) {
    timer->second->enableTimer(period->second);
  }
}

Wasm::~Wasm() {
  lifecycle_stats_handler_.onEvent(WasmEvent::VmShutDown);
  ENVOY_LOG(debug, "~Wasm {} remaining active", lifecycle_stats_handler_.getActiveVmCount());
  if (server_shutdown_post_cb_) {
    dispatcher_.post(std::move(server_shutdown_post_cb_));
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
Word resolve_dns(Word dns_address_ptr, Word dns_address_size, Word token_ptr) {
  auto context = static_cast<Context*>(proxy_wasm::contextOrEffectiveContext());
  auto root_context = context->isRootContext() ? context : context->rootContext();
  auto address = context->wasmVm()->getMemory(dns_address_ptr, dns_address_size);
  if (!address) {
    return WasmResult::InvalidMemoryAccess;
  }
  // Verify set and verify token_ptr before initiating the async resolve.
  uint32_t token = context->wasm()->nextDnsToken();
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto callback = [weak_wasm = std::weak_ptr<Wasm>(context->wasm()->sharedThis()), root_context,
                   context_id = context->id(),
                   token](Envoy::Network::DnsResolver::ResolutionStatus status,
                          std::list<Envoy::Network::DnsResponse>&& response) {
    auto wasm = weak_wasm.lock();
    if (!wasm) {
      return;
    }
    root_context->onResolveDns(token, status, std::move(response));
  };
  if (!context->wasm()->dnsResolver()) {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    Network::DnsResolverFactory& dns_resolver_factory =
        Network::createDefaultDnsResolverFactory(typed_dns_resolver_config);
    context->wasm()->dnsResolver() = dns_resolver_factory.createDnsResolver(
        context->wasm()->dispatcher(), context->wasm()->api(), typed_dns_resolver_config);
  }
  context->wasm()->dnsResolver()->resolve(std::string(address.value()),
                                          Network::DnsLookupFamily::Auto, callback);
  return WasmResult::Ok;
}

void Wasm::registerCallbacks() {
  WasmBase::registerCallbacks();
#define _REGISTER(_fn)                                                                             \
  wasm_vm_->registerCallback(                                                                      \
      "env", "envoy_" #_fn, &_fn,                                                                  \
      &proxy_wasm::ConvertFunctionWordToUint32<decltype(_fn), _fn>::convertFunctionWordToUint32)
  _REGISTER(resolve_dns);
#undef _REGISTER
}

void Wasm::getFunctions() {
  WasmBase::getFunctions();
#define _GET(_fn) wasm_vm_->getFunction("envoy_" #_fn, &_fn##_);
  _GET(on_resolve_dns)
  _GET(on_stats_update)
#undef _GET
}

proxy_wasm::CallOnThreadFunction Wasm::callOnThreadFunction() {
  auto& dispatcher = dispatcher_;
  return [&dispatcher](const std::function<void()>& f) { return dispatcher.post(f); };
}

ContextBase* Wasm::createContext(const std::shared_ptr<PluginBase>& plugin) {
  if (create_context_for_testing_) {
    return create_context_for_testing_(this, std::static_pointer_cast<Plugin>(plugin));
  }
  return new Context(this, std::static_pointer_cast<Plugin>(plugin));
}

ContextBase* Wasm::createRootContext(const std::shared_ptr<PluginBase>& plugin) {
  if (create_root_context_for_testing_) {
    return create_root_context_for_testing_(this, std::static_pointer_cast<Plugin>(plugin));
  }
  return new Context(this, std::static_pointer_cast<Plugin>(plugin));
}

ContextBase* Wasm::createVmContext() { return new Context(this); }

void Wasm::log(const PluginSharedPtr& plugin, const Formatter::HttpFormatterContext& log_context,
               const StreamInfo::StreamInfo& info) {
  auto context = getRootContext(plugin, true);
  context->log(log_context, info);
}

void Wasm::onStatsUpdate(const PluginSharedPtr& plugin, Envoy::Stats::MetricSnapshot& snapshot) {
  auto context = getRootContext(plugin, true);
  context->onStatsUpdate(snapshot);
}

void clearCodeCacheForTesting() {
  std::lock_guard<std::mutex> guard(code_cache_mutex);
  if (code_cache) {
    delete code_cache;
    code_cache = nullptr;
  }
  getCreateStatsHandler().resetStatsForTesting();
}

// TODO: remove this post #4160: Switch default to SimulatedTimeSystem.
void setTimeOffsetForCodeCacheForTesting(MonotonicTime::duration d) {
  cache_time_offset_for_testing = d;
}

static proxy_wasm::WasmHandleFactory
getWasmHandleFactory(WasmConfig& wasm_config, const Stats::ScopeSharedPtr& scope, Api::Api& api,
                     Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher,
                     Server::ServerLifecycleNotifier& lifecycle_notifier) {
  return [&wasm_config, &scope, &api, &cluster_manager, &dispatcher,
          &lifecycle_notifier](std::string_view vm_key) -> WasmHandleBaseSharedPtr {
    auto wasm = std::make_shared<Wasm>(wasm_config, toAbslStringView(vm_key), scope, api,
                                       cluster_manager, dispatcher);
    wasm->initializeLifecycle(lifecycle_notifier);
    return std::static_pointer_cast<WasmHandleBase>(std::make_shared<WasmHandle>(std::move(wasm)));
  };
}

static proxy_wasm::WasmHandleCloneFactory
getWasmHandleCloneFactory(Event::Dispatcher& dispatcher,
                          CreateContextFn create_root_context_for_testing) {
  return [&dispatcher, create_root_context_for_testing](
             WasmHandleBaseSharedPtr base_wasm) -> std::shared_ptr<WasmHandleBase> {
    auto wasm = std::make_shared<Wasm>(std::static_pointer_cast<WasmHandle>(base_wasm), dispatcher);
    wasm->setCreateContextForTesting(nullptr, create_root_context_for_testing);
    return std::static_pointer_cast<WasmHandleBase>(std::make_shared<WasmHandle>(std::move(wasm)));
  };
}

static proxy_wasm::PluginHandleFactory getPluginHandleFactory() {
  return [](WasmHandleBaseSharedPtr base_wasm,
            PluginBaseSharedPtr base_plugin) -> std::shared_ptr<PluginHandleBase> {
    return std::static_pointer_cast<PluginHandleBase>(
        std::make_shared<PluginHandle>(std::static_pointer_cast<WasmHandle>(base_wasm),
                                       std::static_pointer_cast<Plugin>(base_plugin)));
  };
}

WasmEvent toWasmEvent(const std::shared_ptr<WasmHandleBase>& wasm) {
  if (!wasm) {
    return WasmEvent::UnableToCreateVm;
  }
  switch (wasm->wasm()->fail_state()) {
  case FailState::Ok:
    return WasmEvent::Ok;
  case FailState::UnableToCreateVm:
    return WasmEvent::UnableToCreateVm;
  case FailState::UnableToCloneVm:
    return WasmEvent::UnableToCloneVm;
  case FailState::MissingFunction:
    return WasmEvent::MissingFunction;
  case FailState::UnableToInitializeCode:
    return WasmEvent::UnableToInitializeCode;
  case FailState::StartFailed:
    return WasmEvent::StartFailed;
  case FailState::ConfigureFailed:
    return WasmEvent::ConfigureFailed;
  case FailState::RuntimeError:
    return WasmEvent::RuntimeError;
  }
  PANIC("corrupt enum");
}

bool createWasm(const PluginSharedPtr& plugin, const Stats::ScopeSharedPtr& scope,
                Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                Event::Dispatcher& dispatcher, Api::Api& api,
                Server::ServerLifecycleNotifier& lifecycle_notifier,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& cb, CreateContextFn create_root_context_for_testing) {
  auto& stats_handler = getCreateStatsHandler();
  std::string source, code;
  auto config = plugin->wasmConfig();
  auto vm_config = config.config().vm_config();
  bool fetch = false;
  if (vm_config.code().has_remote()) {
    // TODO(https://github.com/envoyproxy/envoy/issues/25052) Stabilize this feature.
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), warn,
                        "Wasm remote code fetch is unstable and may cause a crash");
    auto now = dispatcher.timeSource().monotonicTime() + cache_time_offset_for_testing;
    source = vm_config.code().remote().http_uri().uri();
    std::lock_guard<std::mutex> guard(code_cache_mutex);
    if (!code_cache) {
      code_cache = new std::remove_reference<decltype(*code_cache)>::type;
    }
    Stats::ScopeSharedPtr create_wasm_stats_scope = stats_handler.lockAndCreateStats(scope);
    // Remove entries older than CODE_CACHE_SECONDS_CACHING_TTL except for our target.
    for (auto it = code_cache->begin(); it != code_cache->end();) {
      if (now - it->second.use_time > std::chrono::seconds(CODE_CACHE_SECONDS_CACHING_TTL) &&
          it->first != vm_config.code().remote().sha256()) {
        code_cache->erase(it++);
      } else {
        ++it;
      }
    }
    stats_handler.onRemoteCacheEntriesChanged(code_cache->size());
    auto it = code_cache->find(vm_config.code().remote().sha256());
    if (it != code_cache->end()) {
      it->second.use_time = now;
      if (it->second.in_progress) {
        stats_handler.onEvent(WasmEvent::RemoteLoadCacheMiss);
        ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), warn,
                            "createWasm: failed to load (in progress) from {}", source);
        cb(nullptr);
      }
      code = it->second.code;
      if (code.empty()) {
        if (now - it->second.fetch_time <
            std::chrono::seconds(CODE_CACHE_SECONDS_NEGATIVE_CACHING)) {
          stats_handler.onEvent(WasmEvent::RemoteLoadCacheNegativeHit);
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), warn,
                              "createWasm: failed to load (cached) from {}", source);
          cb(nullptr);
        }
        fetch = true; // Fetch failed, retry.
        it->second.in_progress = true;
        it->second.fetch_time = now;
      } else {
        stats_handler.onEvent(WasmEvent::RemoteLoadCacheHit);
      }
    } else {
      fetch = true; // Not in cache, fetch.
      auto& e = (*code_cache)[vm_config.code().remote().sha256()];
      e.in_progress = true;
      e.use_time = e.fetch_time = now;
      stats_handler.onRemoteCacheEntriesChanged(code_cache->size());
      stats_handler.onEvent(WasmEvent::RemoteLoadCacheMiss);
    }
  } else if (vm_config.code().has_local()) {
    code = Config::DataSource::read(vm_config.code().local(), true, api);
    source = Config::DataSource::getPath(vm_config.code().local())
                 .value_or(code.empty() ? EMPTY_STRING : INLINE_STRING);
  }

  auto vm_key = proxy_wasm::makeVmKey(vm_config.vm_id(),
                                      MessageUtil::anyToBytes(vm_config.configuration()), code);
  auto complete_cb = [cb, vm_key, plugin, scope, &api, &cluster_manager, &dispatcher,
                      &lifecycle_notifier, create_root_context_for_testing,
                      &stats_handler](std::string code) -> bool {
    if (code.empty()) {
      cb(nullptr);
      return false;
    }

    auto config = plugin->wasmConfig();
    auto wasm = proxy_wasm::createWasm(
        vm_key, code, plugin,
        getWasmHandleFactory(config, scope, api, cluster_manager, dispatcher, lifecycle_notifier),
        getWasmHandleCloneFactory(dispatcher, create_root_context_for_testing),
        config.config().vm_config().allow_precompiled());
    Stats::ScopeSharedPtr create_wasm_stats_scope = stats_handler.lockAndCreateStats(scope);
    stats_handler.onEvent(toWasmEvent(wasm));
    if (!wasm || wasm->wasm()->isFailed()) {
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), trace,
                          "Unable to create Wasm");
      cb(nullptr);
      return false;
    }
    cb(std::static_pointer_cast<WasmHandle>(wasm));
    return true;
  };

  if (fetch) {
    auto holder = std::make_shared<std::unique_ptr<Event::DeferredDeletable>>();
    auto fetch_callback = [vm_config, complete_cb, source, &dispatcher, scope, holder, plugin,
                           &stats_handler](const std::string& code) {
      {
        std::lock_guard<std::mutex> guard(code_cache_mutex);
        auto& e = (*code_cache)[vm_config.code().remote().sha256()];
        e.in_progress = false;
        e.code = code;
        Stats::ScopeSharedPtr create_wasm_stats_scope = stats_handler.lockAndCreateStats(scope);
        if (code.empty()) {
          stats_handler.onEvent(WasmEvent::RemoteLoadCacheFetchFailure);
        } else {
          stats_handler.onEvent(WasmEvent::RemoteLoadCacheFetchSuccess);
        }
        stats_handler.onRemoteCacheEntriesChanged(code_cache->size());
      }
      // NB: xDS currently does not support failing asynchronously, so we fail immediately
      // if remote Wasm code is not cached and do a background fill.
      if (!vm_config.nack_on_code_cache_miss()) {
        if (code.empty()) {
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), trace,
                              "Failed to load Wasm code (fetch failed) from {}", source);
        }
        complete_cb(code);
      }
      // NB: must be deleted explicitly.
      if (*holder) {
        dispatcher.deferredDelete(Envoy::Event::DeferredDeletablePtr{holder->release()});
      }
    };
    if (vm_config.nack_on_code_cache_miss()) {
      auto adapter = std::make_unique<RemoteDataFetcherAdapter>(fetch_callback);
      auto fetcher = std::make_unique<Config::DataFetcher::RemoteDataFetcher>(
          cluster_manager, vm_config.code().remote().http_uri(), vm_config.code().remote().sha256(),
          *adapter);
      auto fetcher_ptr = fetcher.get();
      adapter->setFetcher(std::move(fetcher));
      *holder = std::move(adapter);
      fetcher_ptr->fetch();
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), trace,
                          fmt::format("Failed to load Wasm code (fetching) from {}", source));
      cb(nullptr);
      return false;
    } else {
      remote_data_provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
          cluster_manager, init_manager, vm_config.code().remote(), dispatcher,
          api.randomGenerator(), true, fetch_callback);
    }
  } else {
    return complete_cb(code);
  }
  return true;
}

PluginHandleSharedPtr
getOrCreateThreadLocalPlugin(const WasmHandleSharedPtr& base_wasm, const PluginSharedPtr& plugin,
                             Event::Dispatcher& dispatcher,
                             CreateContextFn create_root_context_for_testing) {
  if (!base_wasm) {
    if (!plugin->fail_open_) {
      ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), critical,
                          "Plugin configured to fail closed failed to load");
    }
    // To handle the case when failed to create VMs and fail-open/close properly,
    // we still create PluginHandle with null WasmBase.
    return std::make_shared<PluginHandle>(nullptr, plugin);
  }
  return std::static_pointer_cast<PluginHandle>(proxy_wasm::getOrCreateThreadLocalPlugin(
      std::static_pointer_cast<WasmHandle>(base_wasm), plugin,
      getWasmHandleCloneFactory(dispatcher, create_root_context_for_testing),
      getPluginHandleFactory()));
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
