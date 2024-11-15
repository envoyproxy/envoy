#include "library/common/network/apple_proxy_resolver.h"

#include "source/common/common/assert.h"
#include "source/common/common/posix/thread_impl.h"

namespace Envoy {
namespace Network {

AppleProxyResolver::AppleProxyResolver()
    : proxy_settings_monitor_(
          std::make_unique<AppleSystemProxySettingsMonitor>(proxySettingsUpdater())),
      pac_proxy_resolver_(std::make_unique<ApplePacProxyResolver>()),
      thread_factory_(Thread::PosixThreadFactory::create()) {}

AppleProxyResolver::~AppleProxyResolver() {
  // Wait for the PAC resolution threads to finish.
  for (auto& [_, thread] : pac_resolution_threads_) {
    if (thread->joinable()) {
      thread->join();
    }
  }
}

SystemProxySettingsReadCallback AppleProxyResolver::proxySettingsUpdater() {
  return SystemProxySettingsReadCallback(
      [this](absl::optional<SystemProxySettings> proxy_settings) {
        absl::MutexLock l(&mutex_);
        proxy_settings_ = std::move(proxy_settings);
      });
}

void AppleProxyResolver::start() {
  proxy_settings_monitor_->start();
  started_ = true;
}

void AppleProxyResolver::setDispatcher(Event::Dispatcher* dispatcher) { dispatcher_ = dispatcher; }

ProxyResolutionResult
AppleProxyResolver::resolveProxy(const std::string& target_url_string,
                                 std::vector<ProxySettings>& proxies,
                                 ProxySettingsResolvedCallback proxy_resolution_completed) {
  ASSERT(started_, "AppleProxyResolver not started.");
  ASSERT(dispatcher_ != nullptr, "Dispatcher not set on the AppleProxyResolver.");

  std::string pac_file_url;
  {
    absl::MutexLock l(&mutex_);
    if (!proxy_settings_.has_value()) {
      return ProxyResolutionResult::NoProxyConfigured;
    }

    const auto proxy_settings = proxy_settings_.value();
    if (!proxy_settings.isPacEnabled()) {
      ProxySettings settings(proxy_settings.hostname(), proxy_settings.port());
      if (settings.isDirect()) {
        return ProxyResolutionResult::NoProxyConfigured;
      }
      proxies.emplace_back(std::move(settings));
      return ProxyResolutionResult::ResultCompleted;
    }

    pac_file_url = proxy_settings.pacFileUrl();
  }

  // TODO(abeyad): As stated in ApplePacProxyResolver's comments,
  // CFNetworkExecuteProxyAutoConfigurationURL caches PAC file resolution, so it's not fetched on
  // every request. However, it would be good to not depend on Apple's undocumented behavior and
  // implement a caching layer with TTLs for PAC file URL resolution within AppleProxyResolver
  // itself.
  ASSERT(!pac_file_url.empty(), "PAC file URL must not be empty if PAC is enabled.");
  Thread::PosixThreadPtr thread = thread_factory_->createThread(
      [this, pac_file_url, target_url_string, callback = std::move(proxy_resolution_completed)] {
        pac_proxy_resolver_->resolveProxies(
            pac_file_url, target_url_string,
            [this, callback](const std::vector<ProxySettings>& proxies) {
              auto pac_resolve_thread_id = thread_factory_->currentPthreadId();
              dispatcher_->post([this, callback, proxies, pac_resolve_thread_id] {
                auto thread_handle = pac_resolution_threads_.extract(pac_resolve_thread_id);
                if (!thread_handle.empty()) {
                  thread_handle.mapped()->join();
                }

                callback(proxies);
              });
            });
      },
      /* options= */ absl::nullopt, /* crash_on_failure=*/false);
  Thread::ThreadId thread_id = thread->pthreadId();
  pac_resolution_threads_[thread_id] = std::move(thread);

  return ProxyResolutionResult::ResultInProgress;
}

} // namespace Network
} // namespace Envoy
