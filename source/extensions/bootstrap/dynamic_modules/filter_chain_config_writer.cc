#include "source/extensions/bootstrap/dynamic_modules/filter_chain_config_writer.h"

#include <chrono>
#include <utility>

#include "envoy/config/listener/v3/listener_components.pb.validate.h"
#include "envoy/init/manager.h"

#include "source/common/common/fmt.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

static_assert(static_cast<int>(WriteOutcome::Active) ==
              envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Active);
static_assert(
    static_cast<int>(WriteOutcome::Superseded) ==
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Superseded);
static_assert(static_cast<int>(WriteOutcome::Removed) ==
              envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Removed);
static_assert(static_cast<int>(WriteOutcome::Failed) ==
              envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Failed);
static_assert(
    static_cast<int>(RemovalResult::Success) ==
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_Success);
static_assert(
    static_cast<int>(RemovalResult::NotAllowed) ==
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_NotAllowed);
static_assert(
    static_cast<int>(RemovalResult::NotFound) ==
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_NotFound);

DynamicModuleFilterChainConfigWriter::DynamicModuleFilterChainConfigWriter(
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateDoneCallback update_done)
    : ResourceWriter(extension_name, context, std::move(update_done),
                     {"filter_chain_config_writer", "filter_chains_owned",
                      /*ownership_can_be_lost=*/false},
                     /*resource_type_url=*/""),
      init_target_(
          absl::StrCat("dynamic_modules_bootstrap_filter_chain_config_writer:", config_name_),
          [this]() {
            onClustersInitialized();
            init_target_.ready();
          }) {}

void DynamicModuleFilterChainConfigWriter::onServerInitialized(
    Server::ListenerManager& listener_manager) {
  listener_manager_ = &listener_manager;
  server_initialized_ = true;
  // The server init manager starts once Envoy initialized its static and CDS clusters, which the
  // routes of a filter chain can refer to.
  if (context_.initManager().state() == Init::Manager::State::Uninitialized) {
    context_.initManager().add(init_target_);
  } else {
    onClustersInitialized();
  }
}

void DynamicModuleFilterChainConfigWriter::onClustersInitialized() {
  // Only a running server starts its init manager, and it calls onShutdown() before it frees the
  // listener manager.
  if (!shut_down_) {
    listener_update_callbacks_handle_ = listener_manager_->addListenerUpdateCallbacks(*this);
  }
  startApplying();
}

void DynamicModuleFilterChainConfigWriter::onShutdown() {
  // The listener manager goes away before this writer does.
  listener_update_callbacks_handle_.reset();
  shutDown();
}

envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result
DynamicModuleFilterChainConfigWriter::addOrUpdateFilterChain(absl::string_view filter_chain_name,
                                                             absl::string_view filter_chain,
                                                             uint64_t& update_id) {
  return queueWrite(filter_chain_name, filter_chain, /*version_info=*/"", update_id)
             ? envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result_Success
             : envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result_NotAllowed;
}

envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result
DynamicModuleFilterChainConfigWriter::removeFilterChain(absl::string_view filter_chain_name) {
  return static_cast<envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result>(
      queueRemoval(filter_chain_name));
}

absl::Status DynamicModuleFilterChainConfigWriter::validateUpdate(
    const WriterOperation& operation,
    envoy::config::listener::v3::FilterChain& filter_chain) const {
  if (operation.name.empty()) {
    return absl::InvalidArgumentError("filter chain names must not be empty");
  }
  return parseAndValidate(operation, "filter chain", filter_chain);
}

void DynamicModuleFilterChainConfigWriter::applyUpdate(WriterOperation& operation) {
  WriterResourceState& state = states_.find(operation.name)->second;
  envoy::config::listener::v3::FilterChain filter_chain;
  if (absl::Status status = validateUpdate(operation, filter_chain); !status.ok()) {
    ENVOY_LOG_PERIODIC(
        warn, std::chrono::seconds(10),
        "dynamic module filter chain config writer {} rejected update {} of filter chain {}: {}",
        config_name_, operation.update_id, operation.name, status.message());
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Failed, status.message());
    eraseIfUnused(operation.name);
    return;
  }

  const absl::StatusOr<bool> update_or_error = applyWithManager(
      operation.name, [&]() { return listener_manager_->addOrUpdateFilterChain(filter_chain); });

  if (!update_or_error.ok()) {
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(10),
                       "dynamic module filter chain config writer {} failed to apply update {} of "
                       "filter chain {}: {}",
                       config_name_, operation.update_id, operation.name,
                       update_or_error.status().message());
    // The listener manager keeps the warming and active versions of the filter chain. Only the
    // error of an FCDS name conflict is free of the contents of the filter chain.
    completeUpdate(
        operation.update_id, operation.name, WriteOutcome::Failed,
        absl::IsAlreadyExists(update_or_error.status())
            ? std::string(update_or_error.status().message())
            : fmt::format("the listener manager rejected filter chain {}", operation.name));
    eraseIfUnused(operation.name);
    return;
  }

  setOwned(state, true);
  // An unchanged write with nothing warming matches the active configuration.
  if (!update_or_error.value() && state.warming_update_id == 0) {
    completeUpdate(operation.update_id, operation.name, WriteOutcome::Active);
    return;
  }
  // This write replaces the warming one, even when the listener manager found them identical.
  completeWarmingUpdate(operation.name, state, WriteOutcome::Superseded);
  state.warming_update_id = operation.update_id;
}

void DynamicModuleFilterChainConfigWriter::applyRemoval(const WriterOperation& operation) {
  WriterResourceState& state = states_.find(operation.name)->second;
  if (state.owned) {
    setOwned(state, false);
    completeWarmingUpdate(operation.name, state, WriteOutcome::Removed);
    listener_manager_->removeFilterChain(operation.name);
  }
  eraseIfUnused(operation.name);
}

void DynamicModuleFilterChainConfigWriter::onFilterChainAddOrUpdate(
    absl::string_view filter_chain_name) {
  auto it = states_.find(filter_chain_name);
  if (it != states_.end()) {
    completeWarmingUpdate(filter_chain_name, it->second, WriteOutcome::Active);
  }
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
