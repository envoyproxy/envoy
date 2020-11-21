#pragma once

#include "envoy/server/admin.h"
#include "envoy/singleton/manager.h"

#include "extensions/common/tap/tap.h"

#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

class AdminHandler;
using AdminHandlerSharedPtr = std::shared_ptr<AdminHandler>;

/**
 * Singleton /tap admin handler for admin management of tap configurations and output. This
 * handler is not installed and active unless the tap configuration specifically configures it.
 * TODO(mattklein123): We should allow the admin handler to always be installed in read only mode
 *                     so it's easier to debug the active tap configuration.
 */
class AdminHandler : public Singleton::Instance,
                     public Extensions::Common::Tap::Sink,
                     Logger::Loggable<Logger::Id::tap> {
public:
  AdminHandler(Server::Admin& admin, Event::Dispatcher& main_thread_dispatcher);
  ~AdminHandler() override;

  /**
   * Get the singleton admin handler. The handler will be created if it doesn't already exist,
   * otherwise the existing handler will be returned.
   */
  static AdminHandlerSharedPtr getSingleton(Server::Admin& admin,
                                            Singleton::Manager& singleton_manager,
                                            Event::Dispatcher& main_thread_dispatcher);

  /**
   * Register a new extension config to the handler so that it can be admin managed.
   * @param config supplies the config to register.
   * @param config_id supplies the ID to use for managing the configuration. Multiple extensions
   *        can use the same ID so they can be managed in aggregate (e.g., an HTTP filter on
   *        many listeners).
   */
  void registerConfig(ExtensionConfig& config, const std::string& config_id);

  /**
   * Unregister an extension config from the handler.
   * @param config supplies the previously registered config.
   */
  void unregisterConfig(ExtensionConfig& config);

  // Extensions::Common::Tap::Sink
  PerTapSinkHandlePtr createPerTapSinkHandle(uint64_t) override {
    return std::make_unique<AdminPerTapSinkHandle>(*this);
  }

private:
  struct AdminPerTapSinkHandle : public PerTapSinkHandle {
    AdminPerTapSinkHandle(AdminHandler& parent) : parent_(parent) {}

    // Extensions::Common::Tap::PerTapSinkHandle
    void submitTrace(TraceWrapperPtr&& trace,
                     envoy::config::tap::v3::OutputSink::Format format) override;

    AdminHandler& parent_;
  };

  struct AttachedRequest {
    AttachedRequest(std::string config_id, const envoy::config::tap::v3::TapConfig& config,
                    Server::AdminStream* admin_stream)
        : config_id_(std::move(config_id)), config_(config), admin_stream_(admin_stream) {}

    const std::string config_id_;
    const envoy::config::tap::v3::TapConfig config_;
    const Server::AdminStream* admin_stream_;
  };

  Http::Code handler(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                     Buffer::Instance& response, Server::AdminStream& admin_stream);
  Http::Code badRequest(Buffer::Instance& response, absl::string_view error);

  Server::Admin& admin_;
  Event::Dispatcher& main_thread_dispatcher_;
  absl::node_hash_map<std::string, absl::node_hash_set<ExtensionConfig*>> config_id_map_;
  absl::optional<const AttachedRequest> attached_request_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
