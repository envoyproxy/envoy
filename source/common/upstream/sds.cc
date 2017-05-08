#include "common/upstream/sds.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/http/headers.h"
#include "common/json/config_schemas.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Upstream {

SdsClusterImpl::SdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime,
                               Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                               const SdsConfig& sds_config, const LocalInfo::LocalInfo& local_info,
                               ClusterManager& cm, Event::Dispatcher& dispatcher,
                               Runtime::RandomGenerator& random)
    : BaseDynamicClusterImpl(config, runtime, stats, ssl_context_manager),
      RestApiFetcher(cm, sds_config.sds_cluster_name_, dispatcher, random,
                     sds_config.refresh_delay_),
      local_info_(local_info), service_name_(config.getString("service_name")) {}

void SdsClusterImpl::parseResponse(const Http::Message& response) {
  Json::ObjectPtr json = Json::Factory::loadFromString(response.bodyAsString());
  json->validateSchema(Json::Schema::SDS_SCHEMA);
  std::vector<HostSharedPtr> new_hosts;
  for (const Json::ObjectPtr& host : json->getObjectArray("hosts")) {
    bool canary = false;
    uint32_t weight = 1;
    std::string zone = "";
    if (host->hasObject("tags")) {
      canary = host->getObject("tags")->getBoolean("canary", canary);
      weight = host->getObject("tags")->getInteger("load_balancing_weight", weight);
      zone = host->getObject("tags")->getString("az", zone);
    }

    new_hosts.emplace_back(new HostImpl(
        info_, "", Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(
                       host->getString("ip_address"), host->getInteger("port"))},
        canary, weight, zone));
  }

  HostVectorSharedPtr current_hosts_copy(new std::vector<HostSharedPtr>(hosts()));
  std::vector<HostSharedPtr> hosts_added;
  std::vector<HostSharedPtr> hosts_removed;
  if (updateDynamicHostList(new_hosts, *current_hosts_copy, hosts_added, hosts_removed,
                            health_checker_ != nullptr)) {
    log_debug("sds hosts changed for cluster: {} ({})", info_->name(), hosts().size());
    HostListsSharedPtr per_zone(new std::vector<std::vector<HostSharedPtr>>());

    // If local zone name is not defined then skip populating per zone hosts.
    if (!local_info_.zoneName().empty()) {
      std::map<std::string, std::vector<HostSharedPtr>> hosts_per_zone;

      for (HostSharedPtr host : *current_hosts_copy) {
        hosts_per_zone[host->zone()].push_back(host);
      }

      // Populate per_zone hosts only if upstream cluster has hosts in the same zone.
      if (hosts_per_zone.find(local_info_.zoneName()) != hosts_per_zone.end()) {
        per_zone->push_back(hosts_per_zone[local_info_.zoneName()]);

        for (auto& entry : hosts_per_zone) {
          if (local_info_.zoneName() != entry.first) {
            per_zone->push_back(entry.second);
          }
        }
      }
    }

    updateHosts(current_hosts_copy, createHealthyHostList(*current_hosts_copy), per_zone,
                createHealthyHostLists(*per_zone), hosts_added, hosts_removed);

    if (initialize_callback_ && health_checker_ && pending_health_checks_ == 0) {
      pending_health_checks_ = hosts().size();
      ASSERT(pending_health_checks_ > 0);
      health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, bool) -> void {
        if (pending_health_checks_ > 0 && --pending_health_checks_ == 0) {
          initialize_callback_();
          initialize_callback_ = nullptr;
        }
      });
    }
  }

  info_->stats().update_success_.inc();
}

void SdsClusterImpl::onFetchFailure(EnvoyException* e) {
  log_debug("sds refresh failure for cluster: {}", info_->name());
  info_->stats().update_failure_.inc();
  if (e) {
    log().warn("sds parsing error: {}", e->what());
  }
}

void SdsClusterImpl::createRequest(Http::Message& message) {
  log_debug("starting sds refresh for cluster: {}", info_->name());
  info_->stats().update_attempt_.inc();

  message.headers().addStatic(Http::Headers::get().EnvoyCluster, local_info_.clusterName());
  message.headers().addStatic(Http::Headers::get().EnvoyNode, local_info_.nodeName());
  message.headers().addStatic(Http::Headers::get().EnvoyZone, local_info_.zoneName());
  message.headers().insertMethod().value(Http::Headers::get().MethodValues.Get);
  message.headers().insertPath().value("/v1/registration/" + service_name_);
}

void SdsClusterImpl::onFetchComplete() {
  log_debug("sds refresh complete for cluster: {}", info_->name());
  // If we didn't setup to initialize when our first round of health checking is complete, just
  // do it now.
  if (initialize_callback_ && pending_health_checks_ == 0) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // Upstream
