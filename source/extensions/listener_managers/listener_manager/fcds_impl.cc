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
#include "source/extensions/listener_managers/listener_manager/fcds_impl.h"
#include "source/extensions/listener_managers/listener_manager/filter_chain_manager_impl.h"
#include "source/common/config/xds_resource.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Server {

using Envoy::Server::FilterChainPtr;

FcdsApi::FcdsApi(const envoy::config::listener::v3::ListenerFcds& fcds,
                Configuration::FactoryContext& parent_context, Init::Manager& init_manager,
                FilterChainManagerImpl& filter_chain_manager,
                FilterChainFactoryBuilder* fc_builder,
		Stats::Scope& scope)
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
      //scope_(parent_context.listenerScope().createScope(".fcds")),
      scope_(scope.createScope(".fcds")),
      filter_chain_manager_(filter_chain_manager), fc_builder_(fc_builder)
{

  const auto resource_name = getResourceName();

  // For GRPC based FCDS, collection name is mandatory 
  // For File based FCDS, collection name must kept empty. 
  if (fcds_cfg_name_.empty()) {
    fcds_subscription_ = cluster_manager_.subscriptionFactory().subscriptionFromConfigSource(
        fcds.config_source(), Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_,
        {});
  } else {
    const xds::core::v3::ResourceLocator fcds_resource_locator =
        Config::XdsResourceIdentifier::decodeUrl(fcds_cfg_name_);
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

  std::vector<FilterChainPtr> removed_filter_chains;
  std::vector<FilterChainPtr> updated_filter_chains;
  std::vector<FilterChainPtr> all_incoming_filter_chains;
  std::string message;

  TRY_ASSERT_MAIN_THREAD 
    processDeletedFilterChains(removed_resources, removed_filter_chains);
    processAddedOrUpdatedFilterChains(added_resources, updated_filter_chains, all_incoming_filter_chains);

    // Remove the filter chains that got modified in the incoming update
    filter_chain_manager_.removeFilterChains(updated_filter_chains);

    // Add all the filter chain in the update. There should not be any existing with the same name(s)
    // as those are deleted in the above step
    //absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span = new_filter_chains;
    absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span = all_incoming_filter_chains;
    filter_chain_manager_.addFilterChains(nullptr, filter_chain_span, nullptr, *fc_builder_,
                                        filter_chain_manager_, true);

    // Remove the filter chains which were missing in the update but were already existing
    filter_chain_manager_.removeFilterChains(removed_filter_chains);

    // Drain the filter chains which were updated and deleted
    filter_chain_manager_.startDrainingSequenceForListenerFilterChains();
  END_TRY
  // Here we rollback to the state before the config arrived
  catch (const EnvoyException& e)
  {
    absl::StrAppend(&message, e.what(), "\n");
    
    ENVOY_LOG(error, "fcds: Caught exception.. Rolling back the config");
    
    // Since this is a rollback, no filter chain must be drained
    filter_chain_manager_.clearDrainingList();

    // Clean the existing filter chains. Dont drain them though
    std::vector<FilterChainPtr> current_filter_chains = getExistingFilterChainMessageList();
    if (current_filter_chains.size() != 0) {
      filter_chain_manager_.removeFilterChains(current_filter_chains);
    }

    // Construct the list of filter chains exising before the config update arrived.
    // Restore those filter chains
    std::vector<FilterChainPtr> old_filter_chains;
    old_filter_chains.insert(old_filter_chains.begin(), updated_filter_chains.begin(),
		    updated_filter_chains.end());
    old_filter_chains.insert(old_filter_chains.begin(), removed_filter_chains.begin(),
		    removed_filter_chains.end());
    if (old_filter_chains.size() != 0) {
      filter_chain_manager_.addFilterChains(nullptr, old_filter_chains, nullptr, *fc_builder_,
                                        filter_chain_manager_, true);
    }
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
    ENVOY_LOG(debug, "fcds: Missing Filter Chain for fcds config block = {}", fcds_cfg_name_);
    local_init_target_.ready();
    return false;
  }

  ENVOY_LOG(debug, "fcds: received {} filter chain for fcds config block = {}", num_resources, fcds_cfg_name_);
  return true;
}


// The deleted/draining FC list is constructed here.
void FcdsApi::processDeletedFilterChains(const Protobuf::RepeatedPtrField<std::string>& removed_resources,
  std::vector<FilterChainPtr>& removed_filter_chains) {
  for (const std::string& filter_chain_name : removed_resources) {
    markFilterChainForDeletionIfExists(filter_chain_name, removed_filter_chains);
    ENVOY_LOG(debug, "fcds: removing filter chain from FCM: fcds config block name= {} filter chain={}", 
        fcds_cfg_name_, filter_chain_name);
  }
}

// The updated/added FC list is constructed here.
void FcdsApi::processAddedOrUpdatedFilterChains(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
  std::vector<FilterChainPtr>& updated_filter_chains, 
  std::vector<FilterChainPtr>& new_filter_chains) {
  
  absl::flat_hash_set<std::string> seen_filter_chains; 

  for (const auto& fcds_resource : added_resources) {
    const envoy::config::listener::v3::FilterChain* new_filter_chain =
        dynamic_cast<const envoy::config::listener::v3::FilterChain*>(&(fcds_resource.get().resource()));

    const std::string new_filter_chain_name = new_filter_chain->name();
    
    // If the filter chain name is missing, reject the update
    if (new_filter_chain->name().size() == 0) {
      throw EnvoyException(fmt::format("missing name in one or more filter chain"));
    }

    // If there are two filter chains with the same name, reject the update
    //if (std::find(seen_filter_chains.begin(), seen_filter_chains.end(), new_filter_chain_name)
    if (seen_filter_chains.find(new_filter_chain_name)
        != seen_filter_chains.end()) {
      throw EnvoyException(fmt::format("duplicate filter chain = {}", new_filter_chain_name));
    }

    seen_filter_chains.insert(seen_filter_chains.end(), new_filter_chain_name);

    // Ignore the config update if the old and new configs are same
    const auto hash = MessageUtil::hash(*new_filter_chain);
    if (hash == filter_chain_manager_.getFilterChainMessageHash(new_filter_chain_name)) {
        ENVOY_LOG(debug, "fcds: new and old filter chain config are same. Ignoring update for fc={}",
                new_filter_chain_name);
        continue;
    }

    // This filter chain is to be added   
    new_filter_chains.push_back(new_filter_chain);

    // In this API we determine whether this fiter chain was an existing one.
    // If yes, the existing copy is deleted and new copy is added.
    // Here, we basically put the old copy in the deletion list
    markFilterChainForDeletionIfExists(new_filter_chain_name, updated_filter_chains);

    ENVOY_LOG(debug, "fcds: adding/updating the filter chain = {}", new_filter_chain_name);

  }
}

// Adds the filter chain to deletion as well as the draining list
void FcdsApi::markFilterChainForDeletionIfExists(std::string filter_chain_name,
    std::vector<FilterChainPtr>& to_be_deleted_fc_list)
{
    auto& filter_chains_message_by_name = filter_chain_manager_.getExistingFilterChainMessages();
    auto& filter_chains_object_by_message = filter_chain_manager_.filterChainsByMessage();

    auto iter = filter_chains_message_by_name.find(filter_chain_name);
    // Add the fc to delete list if it exists in the current state
    if (iter != filter_chains_message_by_name.end()) {
      // Add the filter chain to the deletion list
      to_be_deleted_fc_list.push_back(&(iter->second));

      // Add the filter chain to the draining list
      auto iter1 = filter_chains_object_by_message.find(iter->second);
      if (iter1 != filter_chains_object_by_message.end()) {
        filter_chain_manager_.addFcToDrainingList(iter1->second);
      }
    }
}

std::vector<FilterChainPtr> FcdsApi::getExistingFilterChainMessageList() {
    auto& filter_chains_message_by_name = filter_chain_manager_.getExistingFilterChainMessages();

    std::vector<FilterChainPtr> current_filter_chains;
    for (auto iter : filter_chains_message_by_name) {
       current_filter_chains.push_back(&(iter.second));
    }

    return current_filter_chains;
}

} // namespace Server
} // namespace Envoy

