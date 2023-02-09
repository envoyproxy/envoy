#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/server/fcds_impl.h"
#include "source/server/filter_chain_manager_impl.h"
#include "source/common/config/xds_resource.h"

namespace Envoy {
namespace Server {

FcdsApi::FcdsApi(const envoy::config::listener::v3::ListenerFcds& fcds,
                Configuration::FactoryContext& parent_context, Init::Manager& init_manager,
                FilterChainManagerImpl& filter_chain_manager,
                FilterChainFactoryBuilder* fc_builder)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>(
          parent_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      fcds_cfg_name_(fcds.collection_name()),
      parent_init_target_(fmt::format("FcdsConfigSubscription parent_init_target {}", fcds_cfg_name_),
                          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(fmt::format("Fcds local_init_watcher {}", fcds.collection_name()),
                          [this]() { parent_init_target_.ready(); }),
      local_init_target_(fmt::format("FcdsConfigSubscription local_init_target {}", fcds_cfg_name_),
                        [this]() { fcds_subscription_->start({fcds_cfg_name_}); }),
      local_init_manager_(fmt::format("FCDS local_init_manager {}", fcds_cfg_name_)),
      init_manager_(init_manager), cluster_manager_(parent_context.clusterManager()),
      scope_(parent_context.listenerScope().createScope("")),
      filter_chain_manager_(filter_chain_manager), fc_builder_(fc_builder)
{

  const auto resource_name = getResourceName();
  const xds::core::v3::ResourceLocator fcds_resource_locator =
          Config::XdsResourceIdentifier::decodeUrl(fcds_cfg_name_);

  if (fcds_resource_locator == nullptr) {
      fcds_subscription_ = cluster_manager_.subscriptionFactory().subscriptionFromConfigSource(
        fcds.config_source(), Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_,
        {});
  } else {
      fcds_subscription_ = cluster_manager_.subscriptionFactory().collectionSubscriptionFromUrl(
        fcds_resource_locator, fcds.config_source(), resource_name, *scope_, *this, resource_decoder_);
  }
}

FcdsApi::~FcdsApi() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();
}

void FcdsApi::initialize() {
  local_init_manager_.add(local_init_target_);
  init_manager_.add(parent_init_target_);
}

void FcdsApi::onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                              const std::string& version_info) {
  if (!validateUpdateSize(resources.size())) {
    return;
  }

  // Construct the list of filter chains to be removed
  std::unordered_set<std::string> deleted_filter_chains;
  auto filter_chains_message_by_name = filter_chain_manager_.getExistingFilterChainMessages();
  auto iter = filter_chains_message_by_name.begin();
  while (iter != filter_chains_message_by_name.end()) {
    deleted_filter_chains.insert(iter->first);
    ++iter;
  }

  // Remove the list of FCs in the incoming FCDS config from the above list
  for (const auto& filter_chain : resources) {
    deleted_filter_chains.erase(filter_chain.get().name());
  }

  Protobuf::RepeatedPtrField<std::string> to_be_deleted_fc_list;
  for (const auto& filter_chain : deleted_filter_chains) {
    *to_be_deleted_fc_list.Add() = filter_chain;
  }

  // Delete the old fcs and add the new fcs
  onConfigUpdate(resources, to_be_deleted_fc_list, version_info);
}

void FcdsApi::onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string&) {

  std::vector<const envoy::config::listener::v3::FilterChain*> removed_filter_chains;
  std::vector<const envoy::config::listener::v3::FilterChain*> new_filter_chains;
  std::string message;

  TRY_ASSERT_MAIN_THREAD 
    processDeletedFilterChains(removed_resources, removed_filter_chains);
    processAddedOrUpdatedFilterChains(added_resources, removed_filter_chains, new_filter_chains);

    // Finally invoke the deletion call
    filter_chain_manager_.removeFilterChains(removed_filter_chains);

    // Finally invoke the addition call
    absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span = new_filter_chains;
    filter_chain_manager_.addFilterChains(filter_chain_span, nullptr, *fc_builder_,
                                        filter_chain_manager_, true);

    // Drain the filter chains which were deleted
    filter_chain_manager_.startDrainingSequenceForListenerFilterChains();
  END_TRY
  catch (const EnvoyException& e)
  {
    absl::StrAppend(&message, e.what(), "\n");

  }

  local_init_target_.ready();

  if (!message.empty()) {
    throw EnvoyException(fmt::format("Error adding/updating filter chain(s) : {}\n", message));
  }
}

void FcdsApi::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                    const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  local_init_target_.ready();
}

bool FcdsApi::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing Filter Chain for fcds config block = {}", fcds_cfg_name_);
    local_init_target_.ready();
    return false;
  }

  ENVOY_LOG(debug, "fcds: received {} filter chain for fcds config block = {}", num_resources, fcds_cfg_name_);
  return true;
}


// The deleted/draining FC list are constructed here.
void FcdsApi::processDeletedFilterChains(const Protobuf::RepeatedPtrField<std::string>& removed_resources,
  std::vector<const envoy::config::listener::v3::FilterChain*>& removed_filter_chains) {
  for (const std::string& filter_chain_name : removed_resources) {
    markFilterChainForDeletion(filter_chain_name, removed_filter_chains);
    ENVOY_LOG(debug, "fcds: removing filter chain from FCM: fcds config block name= {} filter chain={}", 
        fcds_cfg_name_, filter_chain_name);
  }
}

// The updated/added FC list are constructed here.
void FcdsApi::processAddedOrUpdatedFilterChains(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
  std::vector<const envoy::config::listener::v3::FilterChain*>& updated_filter_chains, 
  std::vector<const envoy::config::listener::v3::FilterChain*>& new_filter_chains) {
  
  std::vector<std::string> seen_filter_chains; 

  for (const auto& fcds_resource : added_resources) {
    envoy::config::listener::v3::FilterChain new_filter_chain =
        dynamic_cast<const envoy::config::listener::v3::FilterChain&>(fcds_resource.get().resource());

    // If filter chanin name is missing, reject the update
    if (new_filter_chain.name().size() == 0) {
      throw EnvoyException(fmt::format("missing filter chain(s) name for fcds config block = {}", fcds_cfg_name_));
    }

    // If there are two filter chains with the same name, reject the update
    const std::string new_filter_chain_name = new_filter_chain.name();
    if (std::find(seen_filter_chains.begin(), seen_filter_chains.end(), new_filter_chain_name)
        != seen_filter_chains.end()) {
      throw EnvoyException(fmt::format("duplicate filter chain = {}", new_filter_chain_name));
    }

    seen_filter_chains.push_back(new_filter_chain_name);

    // Ignore the config update if the old and new configs are same
    const auto hash = MessageUtil::hash(new_filter_chain);
    if (hash == filter_chain_manager_.getFilterChainMessageHash(new_filter_chain_name)) {
        ENVOY_LOG(debug, "fcds: new and old filter chain config are same. Ignoring update for fc={}",
                new_filter_chain_name);
        continue;
    }

    // This filter chain is to be added   
    new_filter_chains.push_back(dynamic_cast<const envoy::config::listener::v3::FilterChain*>(
        &(fcds_resource.get().resource())));

    // If this is an update request for an existing filter chain, it will checked and marked for deletion here
    markFilterChainForDeletion(new_filter_chain_name, updated_filter_chains);

    ENVOY_LOG(debug, "fcds: adding/updating the filter chain = {}", new_filter_chain_name);

  }
}

// Adds the filter chain to deletion as well as draining list
void FcdsApi::markFilterChainForDeletion(std::string filter_chain_name,
    std::vector<const envoy::config::listener::v3::FilterChain*>& updated_filter_chains)
{
      auto& filter_chains_message_by_name = filter_chain_manager_.getExistingFilterChainMessages();
      auto& filter_chains_object_by_message = filter_chain_manager_.filterChainsByMessage();

      auto iter = filter_chains_message_by_name.find(filter_chain_name);
      if (iter != filter_chains_message_by_name.end()) {
        updated_filter_chains.push_back(&(iter->second));

        auto iter1 = filter_chains_object_by_message.find(iter->second);
        if (iter1 != filter_chains_object_by_message.end()) {
          filter_chain_manager_.addFcToDrainingList(iter1->second);
        }
      }
}

} // namespace Server
} // namespace Envoy

