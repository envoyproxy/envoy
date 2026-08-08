#include "source/common/rds/route_config_update_receiver_impl.h"

#include <functional>

#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

void ConfigWarmer::updateInitManager(std::unique_ptr<Init::ManagerImpl> init_manager,
                                     absl::string_view update_id) {
  if (init_manager_ != nullptr) {
    // A previous update is still warming up. This update supersedes it: the route configuration it
    // would have published has already been replaced, so drop its watcher and init manager and
    // never publish it.
    ENVOY_LOG(debug,
              "rds: update {} superseded update {} while it was still warming up, abandoning "
              "the superseded update",
              update_id, update_id_);
    init_watcher_.reset();
    mayDeferDeleteInitManager();
  }
  update_id_ = std::string(update_id);
  // Assigning the watcher first drops the abandoned watcher while its init manager is still around.
  // That is safe, the manager only holds a weak handle to it.
  init_watcher_ = std::make_unique<Init::WatcherImpl>(
      fmt::format("{} update-init-watcher", update_id), [this]() { onWarmed(); });
  init_manager_ = std::move(init_manager);
}

void ConfigWarmer::startWarming() {
  ASSERT(init_manager_ != nullptr);
  // Note this notifies the observer synchronously, i.e. before returning, if there is nothing to
  // warm up. It may therefore drop the init manager and the watcher before returning.
  init_manager_->initialize(*init_watcher_);
}

void ConfigWarmer::onWarmed() {
  // Drop the watcher right away, so that nothing can call back into this warmer.
  init_watcher_.reset();
  // The init manager can't be dropped here because it may be destroyed from inside its own
  // initialize() method.
  mayDeferDeleteInitManager();
  init_manager_.reset();
  update_id_.clear();

  if (on_warmed_callback_) {
    on_warmed_callback_();
  }

  if (observer_.has_value()) {
    observer_->onConfigWarmed();
  }
}

class DeferredDeleteInitManagerCleanup : public Event::DeferredDeletable {
public:
  DeferredDeleteInitManagerCleanup(std::unique_ptr<Init::ManagerImpl> init_manager)
      : init_manager_(std::move(init_manager)) {}
  ~DeferredDeleteInitManagerCleanup() override = default;

private:
  std::unique_ptr<Init::ManagerImpl> init_manager_;
};

void ConfigWarmer::mayDeferDeleteInitManager() {
  if (main_dispatcher_.has_value()) {
    main_dispatcher_->deferredDelete(
        std::make_unique<DeferredDeleteInitManagerCleanup>(std::move(init_manager_)));
  } else {
    init_manager_.reset();
  }
}

RouteConfigUpdateReceiverImpl::RouteConfigUpdateReceiverImpl(
    ConfigTraits& config_traits, ProtoTraits& proto_traits,
    Server::Configuration::ServerFactoryContext& factory_context)
    : config_traits_(config_traits), proto_traits_(proto_traits), factory_context_(factory_context),
      time_source_(factory_context.timeSource()),
      route_config_proto_(proto_traits_.createEmptyProto()),
      config_(config_traits_.createNullConfig()),
      warmer_(factory_context_.mainThreadDispatcher(), [this]() { onConfigWarmed(); }) {}

void RouteConfigUpdateReceiverImpl::updateConfig(
    std::unique_ptr<Protobuf::Message> route_config_proto, std::optional<uint64_t> hash,
    absl::string_view version_info) {

  const std::string update_id =
      fmt::format("rds {}:{}", resourceName(proto_traits_, *route_config_proto), version_info);
  // The init manager is kept local until the new route configuration is known to be built without
  // throwing, so that a rejected update leaves a previous update that is still warming up alone.
  auto update_init_manager = warmer_.createInitManager(update_id);
  auto config =
      config_traits_.createConfig(*route_config_proto, factory_context_, *update_init_manager,
                                  false /* not validate unknown cluster */);

  warming_state_.route_config_proto_ = std::move(route_config_proto);
  warming_state_.config_ = std::move(config);
  warming_state_.last_config_hash_ = hash;
  warming_state_.last_updated_ = time_source_.systemTime();
  warming_state_.version_info_.assign(version_info.data(), version_info.size());

  // The new route configuration has been built but isn't warmed up or published yet. The caller
  // finishes applying the update and then calls startWarming().
  warmer_.updateInitManager(std::move(update_init_manager), update_id);
}

// Rds::RouteConfigUpdateReceiver
bool RouteConfigUpdateReceiverImpl::onRdsUpdate(const Protobuf::Message& rc,
                                                const std::string& version_info) {
  uint64_t new_hash = getHash(rc);
  if (!checkHash(new_hash)) {
    return false;
  }

  updateConfig(cloneProto(proto_traits_, rc), new_hash, version_info);
  startWarming();
  return true;
}

void RouteConfigUpdateReceiverImpl::onConfigWarmed() {
  if (warming_state_.route_config_proto_ != nullptr) {
    route_config_proto_ = std::move(warming_state_.route_config_proto_);
    config_ = std::move(warming_state_.config_);
    // Only update the last config hash if it was set in the warming state.
    if (warming_state_.last_config_hash_.has_value()) {
      last_config_hash_ = warming_state_.last_config_hash_.value();
    }
    last_updated_ = warming_state_.last_updated_;
    config_info_.emplace(*route_config_proto_, std::move(warming_state_.version_info_));
  }
  // Defensively clear the warming state.
  warming_state_.clear();
}

const std::optional<RouteConfigProvider::ConfigInfo>&
RouteConfigUpdateReceiverImpl::configInfo() const {
  return config_info_;
}

} // namespace Rds
} // namespace Envoy
