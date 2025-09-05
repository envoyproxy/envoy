#include "source/common/upstream/od_cds_api_impl.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<OdCdsApiSharedPtr>
OdCdsApiImpl::create(const envoy::config::core::v3::ConfigSource& odcds_config,
                     OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                     Config::XdsManager& xds_manager, ClusterManager& cm,
                     MissingClusterNotifier& notifier, Stats::Scope& scope,
                     ProtobufMessage::ValidationVisitor& validation_visitor,
                     Server::Configuration::ServerFactoryContext&) {
  absl::Status creation_status = absl::OkStatus();
  auto ret =
      OdCdsApiSharedPtr(new OdCdsApiImpl(odcds_config, odcds_resources_locator, xds_manager, cm,
                                         notifier, scope, validation_visitor, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

OdCdsApiImpl::OdCdsApiImpl(const envoy::config::core::v3::ConfigSource& odcds_config,
                           OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                           Config::XdsManager& xds_manager, ClusterManager& cm,
                           MissingClusterNotifier& notifier, Stats::Scope& scope,
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           absl::Status& creation_status)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(validation_visitor,
                                                                           "name"),
      helper_(cm, xds_manager, "odcds"), notifier_(notifier),
      scope_(scope.createScope("cluster_manager.odcds.")) {
  // TODO(krnowak): Move the subscription setup to CdsApiHelper. Maybe make CdsApiHelper a base
  // class for CDS and ODCDS.
  const auto resource_name = getResourceName();
  absl::StatusOr<Config::SubscriptionPtr> subscription_or_error;
  if (!odcds_resources_locator.has_value()) {
    subscription_or_error = cm.subscriptionFactory().subscriptionFromConfigSource(
        odcds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
  } else {
    subscription_or_error = cm.subscriptionFactory().collectionSubscriptionFromUrl(
        *odcds_resources_locator, odcds_config, resource_name, *scope_, *this, resource_decoder_);
  }
  SET_AND_RETURN_IF_NOT_OK(subscription_or_error.status(), creation_status);
  subscription_ = std::move(*subscription_or_error);
}

absl::Status OdCdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                          const std::string& version_info) {
  UNREFERENCED_PARAMETER(resources);
  UNREFERENCED_PARAMETER(version_info);
  // On-demand cluster updates are only supported for delta, not sotw.
  PANIC("not supported");
}

absl::Status
OdCdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                             const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                             const std::string& system_version_info) {
  auto [_, exception_msgs] =
      helper_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  sendAwaiting();
  status_ = StartStatus::InitialFetchDone;
  // According to the XDS specification, the server can send a reply with names in the
  // removed_resources field for requested resources that do not exist. That way we can notify the
  // interested parties about the missing resource immediately without waiting for some timeout to
  // be triggered.
  for (const auto& resource_name : removed_resources) {
    ENVOY_LOG(debug, "odcds: notifying about potential missing cluster {}", resource_name);
    notifier_.notifyMissingCluster(resource_name);
  }
  if (!exception_msgs.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
  return absl::OkStatus();
}

void OdCdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                        const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  sendAwaiting();
  status_ = StartStatus::InitialFetchDone;
}

void OdCdsApiImpl::sendAwaiting() {
  if (awaiting_names_.empty()) {
    return;
  }
  // The awaiting names are sent only once. After the state transition from Starting to
  // InitialFetchDone (which happens on the first received response), the awaiting names list is not
  // used any more.
  ENVOY_LOG(debug, "odcds: sending request for awaiting cluster names {}",
            fmt::join(awaiting_names_, ", "));
  subscription_->requestOnDemandUpdate(awaiting_names_);
  awaiting_names_.clear();
}

void OdCdsApiImpl::updateOnDemand(std::string cluster_name) {
  switch (status_) {
  case StartStatus::NotStarted:
    ENVOY_LOG(trace, "odcds: starting a subscription with cluster name {}", cluster_name);
    status_ = StartStatus::Started;
    subscription_->start({std::move(cluster_name)});
    return;

  case StartStatus::Started:
    ENVOY_LOG(trace, "odcds: putting cluster name {} on awaiting list", cluster_name);
    awaiting_names_.insert(std::move(cluster_name));
    return;

  case StartStatus::InitialFetchDone:
    ENVOY_LOG(trace, "odcds: requesting for cluster name {}", cluster_name);
    subscription_->requestOnDemandUpdate({std::move(cluster_name)});
    return;
  }
}

// A class that maintains all the od-cds xDS-TP based singleton subscriptions,
// and update the cluster-manager when the resources are updated.
// The object will only be accessed by the main thread. It should also be a
// singleton object that is used by all the filters that need to access od-cds
// over xdstp-based config sources, and will only be allocated for the first
// occurrence of the filter.
class XdstpOdCdsApiImpl::XdstpOdcdsSubscriptionsManager : public Singleton::Instance,
                                                          Logger::Loggable<Logger::Id::upstream> {
public:
  XdstpOdcdsSubscriptionsManager(Config::XdsManager& xds_manager, ClusterManager& cm,
                                 MissingClusterNotifier& notifier, Stats::Scope& scope,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
      : xds_manager_(xds_manager), helper_(cm, xds_manager, "odcds-xdstp"), notifier_(notifier),
        scope_(scope.createScope("cluster_manager.odcds.")),
        validation_visitor_(validation_visitor) {}

  absl::Status onResourceUpdate(absl::string_view resource_name,
                                const Config::DecodedResourceRef& resource,
                                const std::string& system_version_info) {
    auto [_, exception_msgs] = helper_.onConfigUpdate({resource}, {}, system_version_info);
    if (!exception_msgs.empty()) {
      return absl::InvalidArgumentError(fmt::format("Error adding/updating cluster {} - {}",
                                                    resource_name,
                                                    absl::StrJoin(exception_msgs, ", ")));
    }
    return absl::OkStatus();
  }

  absl::Status onResourceRemoved(absl::string_view resource_name,
                                 const std::string& system_version_info) {
    // TODO(adisuissa): add direct `onResourceRemove(resource_name)` to `helper_`.
    Protobuf::RepeatedPtrField<std::string> removed_resource_list;
    removed_resource_list.Add(std::string(resource_name));
    auto [_, exception_msgs] =
        helper_.onConfigUpdate({}, removed_resource_list, system_version_info);
    // Removal of a cluster should not result in an error.
    ASSERT(exception_msgs.empty());
    notifier_.notifyMissingCluster(resource_name);
    return absl::OkStatus();
  }

  void onFailure(absl::string_view resource_name) {
    ENVOY_LOG(trace, "ODCDS-manager: failure for resource: {}", resource_name);
    // This function will only be invoked if the resource wasn't previously updated or removed.
    // Remove the resource, so if there are other resources waiting for it,
    // their initialization can proceed.
    notifier_.notifyMissingCluster(resource_name);
  }

  void addSubscription(absl::string_view resource_name) {
    if (subscriptions_.contains(resource_name)) {
      ENVOY_LOG(debug, "ODCDS-manager: resource {} is already subscribed to, skipping",
                resource_name);
      return;
    }
    ENVOY_LOG(trace, "ODCDS-manager: adding a subscription for resource {}", resource_name);
    // Subscribe using the xds-manager.
    auto subscription =
        std::make_unique<PerSubscriptionData>(*this, resource_name, validation_visitor_);
    absl::Status status = subscription->initializeSubscription();
    if (status.ok()) {
      subscriptions_.emplace(std::string(resource_name), std::move(subscription));
    } else {
      // There was an error while subscribing. This could be, for example, when
      // the cluster_name isn't a valid xdstp resource, or its config-source was
      // not added to the bootstrap's config_sources.
      ENVOY_LOG(info,
                "ODCDS-manager: xDS-TP resource {} could not be registered: {}. Treating as "
                "missing cluster",
                resource_name, status.message());
      onFailure(resource_name);
    }
  }

private:
  // A singleton subscription handler.
  class PerSubscriptionData : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster> {
  public:
    PerSubscriptionData(XdstpOdcdsSubscriptionsManager& parent, absl::string_view resource_name,
                        ProtobufMessage::ValidationVisitor& validation_visitor)
        : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(validation_visitor,
                                                                               "name"),
          parent_(parent), resource_name_(resource_name) {}

    absl::Status initializeSubscription() {
      const auto resource_type = getResourceName();
      absl::StatusOr<Config::SubscriptionPtr> subscription_or_error =
          parent_.xds_manager_.subscribeToSingletonResource(
              resource_name_, absl::nullopt, Grpc::Common::typeUrl(resource_type), *parent_.scope_,
              *this, resource_decoder_, {});
      RETURN_IF_NOT_OK_REF(subscription_or_error.status());
      subscription_ = std::move(subscription_or_error.value());
      subscription_->start({resource_name_});
      return absl::OkStatus();
    }

  private:
    // Config::SubscriptionCallbacks
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                const std::string& version_info) override {
      // As this is a singleton subscription, the response can either contain 1
      // resource that should be updated or 0 (implying the resource should be
      // removed).
      ASSERT(resources.empty() || (resources.size() == 1));
      resource_was_updated_ = true;
      if (resources.empty()) {
        ENVOY_LOG(trace, "ODCDS-manager: removing a single resource: {}", resource_name_);
        return parent_.onResourceRemoved(resource_name_, version_info);
      }
      // A single cluster update.
      ENVOY_LOG(trace, "ODCDS-manager: updating a single resource: {}", resource_name_);
      return parent_.onResourceUpdate(resource_name_, resources[0], version_info);
    }
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                const std::string& system_version_info) override {
      // As this is a singleton subscription, the update can either contain 1
      // added resource, or remove the resource.
      ASSERT(added_resources.size() + removed_resources.size() == 1);
      resource_was_updated_ = true;
      if (!removed_resources.empty()) {
        ENVOY_LOG(trace, "ODCDS-manager: removing a single resource: {}", resource_name_);
        return parent_.onResourceRemoved(resource_name_, system_version_info);
      }
      // A single cluster update.
      ENVOY_LOG(trace, "ODCDS-manager: updating a single resource: {}", resource_name_);
      return parent_.onResourceUpdate(resource_name_, added_resources[0], system_version_info);
    }
    void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                              const EnvoyException* e) override {
      ASSERT(reason != Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure);
      ENVOY_LOG(trace, "ODCDS-manager: error while fetching a single resource {}: {}",
                resource_name_, e->what());
      // If the resource wasn't previously updated, this sends a notification that it was removed,
      // so if there are any resources waiting for this one, they can proceed.
      if (!resource_was_updated_) {
        resource_was_updated_ = true;
        parent_.onFailure(resource_name_);
      }
    }

    XdstpOdcdsSubscriptionsManager& parent_;
    // TODO(adisuissa): this can be converted to an absl::string_view and point to the
    // subscriptions_ map key.
    const std::string resource_name_;
    Config::SubscriptionPtr subscription_;
    bool resource_was_updated_{false};
  };
  using PerSubscriptionDataPtr = std::unique_ptr<PerSubscriptionData>;

  Config::XdsManager& xds_manager_;
  CdsApiHelper helper_;
  MissingClusterNotifier& notifier_;
  Stats::ScopeSharedPtr scope_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  // Maps a resource name to its subscription data.
  absl::flat_hash_map<std::string, PerSubscriptionDataPtr> subscriptions_;
};

// Register the XdstpOdcdsSubscriptionsManager singleton.
SINGLETON_MANAGER_REGISTRATION(xdstp_odcds_subscriptions_manager);

absl::StatusOr<OdCdsApiSharedPtr>
XdstpOdCdsApiImpl::create(const envoy::config::core::v3::ConfigSource&,
                          OptRef<xds::core::v3::ResourceLocator>, Config::XdsManager& xds_manager,
                          ClusterManager& cm, MissingClusterNotifier& notifier, Stats::Scope& scope,
                          ProtobufMessage::ValidationVisitor& validation_visitor,
                          Server::Configuration::ServerFactoryContext& server_factory_context) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = OdCdsApiSharedPtr(new XdstpOdCdsApiImpl(xds_manager, cm, notifier, scope,
                                                     server_factory_context, validation_visitor,
                                                     creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

XdstpOdCdsApiImpl::XdstpOdCdsApiImpl(Config::XdsManager& xds_manager, ClusterManager& cm,
                                     MissingClusterNotifier& notifier, Stats::Scope& scope,
                                     Server::Configuration::ServerFactoryContext& server_context,
                                     ProtobufMessage::ValidationVisitor& validation_visitor,
                                     absl::Status& creation_status) {
  // Create a singleton xdstp-based od-cds handler. This will be accessed by
  // the main thread and used by all the filters that need to access od-cds
  // over xdstp-based config sources.
  // The singleton object will handle all the subscriptions to OD-CDS
  // resources, and will apply the updates to the cluster-manager.
  subscriptions_manager_ =
      subscriptionsManager(server_context, xds_manager, cm, notifier, scope, validation_visitor);
  // This will always succeed as the xDS-TP config-source matching the resource name
  // will only be known when that resource is subscribed to.
  creation_status = absl::OkStatus();
}

XdstpOdCdsApiImpl::XdstpOdcdsSubscriptionsManagerSharedPtr
XdstpOdCdsApiImpl::subscriptionsManager(Server::Configuration::ServerFactoryContext& server_context,
                                        Config::XdsManager& xds_manager, ClusterManager& cm,
                                        MissingClusterNotifier& notifier, Stats::Scope& scope,
                                        ProtobufMessage::ValidationVisitor& validation_visitor) {
  return server_context.singletonManager().getTyped<XdstpOdcdsSubscriptionsManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(xdstp_odcds_subscriptions_manager),
      [&xds_manager, &cm, &notifier, &scope, &validation_visitor] {
        return std::make_shared<XdstpOdcdsSubscriptionsManager>(xds_manager, cm, notifier, scope,
                                                                validation_visitor);
      });
}

void XdstpOdCdsApiImpl::updateOnDemand(std::string cluster_name) {
  subscriptions_manager_->addSubscription(cluster_name);
}
} // namespace Upstream
} // namespace Envoy
