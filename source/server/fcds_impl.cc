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

namespace Envoy {
namespace Server {

FcdsApi::FcdsApi(
    const envoy::config::listener::v3::Fcds& fcds,
    Configuration::FactoryContext& parent_context,
    Init::Manager& init_manager,
    FilterChainManagerImpl& filter_chain_manager,
    FilterChainFactoryBuilder* fc_builder)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>(
		    parent_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      fcds_name_(fcds.fcds_name()),
      parent_init_target_(fmt::format("FcdsConfigSubscription parent_init_target {}", fcds_name_),
                          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(fmt::format("Fcds local_init_watcher {}", fcds.fcds_name()),
                          [this]() { parent_init_target_.ready(); }),
      local_init_target_(
          fmt::format("FcdsConfigSubscription local_init_target {}", fcds_name_),
          [this]() { fcds_subscription_->start({fcds_name_}); }),
      local_init_manager_(fmt::format("FCDS local_init_manager {}", fcds_name_)),
      init_manager_(init_manager),
      cluster_manager_(parent_context.clusterManager()),
      scope_(parent_context.listenerScope().createScope("")),
      filter_chain_manager_(filter_chain_manager),
      fc_builder_(fc_builder)
{

  const auto resource_name = getResourceName();
  
  fcds_subscription_ =
      cluster_manager_.subscriptionFactory().subscriptionFromConfigSource(
          fcds.config_source(), Grpc::Common::typeUrl(resource_name), *scope_, *this,
          resource_decoder_, {});

}


FcdsApi::~FcdsApi()
{
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();
}

void FcdsApi::initialize()
{
  local_init_manager_.add(local_init_target_);
  init_manager_.add(parent_init_target_);
}

void FcdsApi::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& resources,
    const std::string& version_info) {

  if (!validateUpdateSize(resources.size())) {
    return;
  }

  // Construct the list of filter chains to be removed
  std::unordered_set<std::string> fc_to_remove;
  auto fc_name_to_resource_map = filter_chain_manager_.getFcdsResources();
  auto itr = fc_name_to_resource_map.begin();
  while (itr != fc_name_to_resource_map.end()) {
	fc_to_remove.insert(itr->first);
	++itr;
  }

  // Remove the list of FCs in the incoming FCDS config from the above list  
  for (const auto& filter_chain : resources)
  {
    fc_to_remove.erase(filter_chain.get().name());
  }

  Protobuf::RepeatedPtrField<std::string> to_be_deleted_fc_list;
  for (const auto& filter_chain : fc_to_remove) {
	*to_be_deleted_fc_list.Add() = filter_chain;
  }

  // Delete the old fcs and add the new fcs
  onConfigUpdate(resources, to_be_deleted_fc_list, version_info);
}


void FcdsApi::onConfigUpdate(
    const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  
  // Get all the existing filter chains
  auto& fc_name_to_resource_map = filter_chain_manager_.getFcdsResources();
  
  // Delete the filter chains which are missing in this config update
  std::vector<envoy::config::listener::v3::FilterChain> fc_to_del_vector;

  for (const std::string& fc_name : removed_resources)
  {
    ENVOY_LOG(debug, "fcds: removing filter chain from FCM: fcds name = {} resource={}", fcds_name_, fc_name);
    auto itr = fc_name_to_resource_map.find(fc_name);
    if (itr != fc_name_to_resource_map.end()) {
	const envoy::config::listener::v3::FilterChain& fc = itr->second;
    	fc_to_del_vector.push_back(fc);
    }
  }

  if (fc_to_del_vector.size()) {
    filter_chain_manager_.removeFilterChains(fc_to_del_vector);
  }

  // Add/Update the filter chains which are part of the new update
  std::vector<const envoy::config::listener::v3::FilterChain*> fc_to_add_vector;
  for (const auto& fcds_resource : added_resources)
  {
    envoy::config::listener::v3::FilterChain fc_config;
    fc_config = dynamic_cast<const envoy::config::listener::v3::FilterChain&>(fcds_resource.get().resource());
    
    if (fc_config.name().size() == 0) {
      ENVOY_LOG(debug, "fcds: skipping this filter chain config update due to missing filter chain name, resource_name={}", fc_config.name());
      continue;
    } 
    fc_to_add_vector.clear(); 
    fc_to_add_vector.push_back(&fc_config); 

    ENVOY_LOG(debug, "fcds: adding filter chain: fcds name = {} resource_name={}", fcds_name_, fc_config.name());
    filter_chain_manager_.addFilterChains(fc_to_add_vector, nullptr, *fc_builder_, filter_chain_manager_, true);
  }

  local_init_target_.ready();
}

void FcdsApi::onConfigUpdateFailed(
    Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  local_init_target_.ready();
}

bool FcdsApi::validateUpdateSize(int num_resources) {
  if (num_resources == 0) {
    ENVOY_LOG(debug, "Missing Filter Chain Configuration for {} in onConfigUpdate()", fcds_name_);
    local_init_target_.ready();
    return false;
  }

  ENVOY_LOG(debug, "fcds: recived {} filter chain for fcds {}", num_resources, fcds_name_);
  return true;
}

} // namespace Server
} // namespace Envoy


