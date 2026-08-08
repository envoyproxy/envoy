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
  deferred_delete_init_manager_ = std::move(init_manager_);
  init_manager_.reset();
  update_id_.clear();

  if (on_warmed_callback_) {
    on_warmed_callback_();
  }

  if (observer_.has_value()) {
    observer_->onConfigWarmed();
  }
}

RouteConfigUpdateReceiverImpl::RouteConfigUpdateReceiverImpl(
    ConfigTraits& config_traits, ProtoTraits& proto_traits,
    Server::Configuration::ServerFactoryContext& factory_context)
    : config_traits_(config_traits), proto_traits_(proto_traits), factory_context_(factory_context),
      time_source_(factory_context.timeSource()), warmer_([this]() { onConfigWarmed(); }),
      current_state_{proto_traits_.createEmptyProto(), config_traits_.createNullConfig(),
                     std::nullopt, 0ull, SystemTime{}} {}

void RouteConfigUpdateReceiverImpl::updateConfig(
    std::unique_ptr<Protobuf::Message> route_config_proto, uint64_t hash,
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
  warming_state_.config_info_.emplace(RouteConfigProvider::ConfigInfo{
      *warming_state_.route_config_proto_, std::string(version_info)});

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
  current_state_.route_config_proto_ = std::move(warming_state_.route_config_proto_);
  current_state_.config_ = std::move(warming_state_.config_);
  current_state_.last_config_hash_ = warming_state_.last_config_hash_;
  current_state_.last_updated_ = warming_state_.last_updated_;
  current_state_.config_info_.emplace(std::move(*warming_state_.config_info_));

  warming_state_.route_config_proto_.reset();
  warming_state_.config_.reset();
  warming_state_.last_config_hash_ = 0ull;
  warming_state_.last_updated_ = SystemTime{};
  warming_state_.config_info_.reset();
}

} // namespace Rds
} // namespace Envoy
