#pragma once

#include "envoy/config/xds_manager.h"

#include "source/common/common/thread.h"
#include "source/common/config/subscription_factory_impl.h"

namespace Envoy {
namespace Config {

class XdsManagerImpl : public XdsManager {
public:
  XdsManagerImpl(Event::Dispatcher& main_thread_dispatcher, Api::Api& api, Stats::Store& stats,
                 const LocalInfo::LocalInfo& local_info,
                 ProtobufMessage::ValidationContext& validation_context, Server::Instance& server)
      : server_(server), main_thread_dispatcher_(main_thread_dispatcher), api_(api),
        random_(api.randomGenerator()), stats_(stats), local_info_(local_info),
        validation_context_(validation_context) {}

  // Config::XdsManager
  absl::Status initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          Upstream::ClusterManager* cm) override;
  absl::Status
  initializeAdsConnections(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;
  void shutdown() override { ads_mux_.reset(); }
  absl::Status
  setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) override;

  Config::GrpcMuxSharedPtr adsMux() override { return ads_mux_; }
  SubscriptionFactory& subscriptionFactory() override { return *subscription_factory_; }

private:
  class AuthorityData {
  public:
    AuthorityData(absl::flat_hash_set<std::string>&& authority_names, GrpcMuxSharedPtr&& grpc_mux)
        : authority_names_(std::move(authority_names)), grpc_mux_(std::move(grpc_mux)) {}

  private:
    // The set of authority names this config-source supports.
    // Note that only the `default_config_source` may have an empty list of authority names.
    absl::flat_hash_set<std::string> authority_names_;
    // The ADS gRPC mux to the server.
    Config::GrpcMuxSharedPtr grpc_mux_;
  };
  using AuthorityDataIterator = std::vector<AuthorityData>::iterator;

  // Adds an authority config source given the authority name.
  // Returns an iterator to the authorities_ vector entry that was added or an error if one
  // occurred.
  absl::StatusOr<AuthorityDataIterator>
  addAuthority(const envoy::config::core::v3::ConfigSource& config_source,
               bool allow_no_authority_names);

  // Validates (syntactically) the config_source by doing the PGV validation.
  absl::Status validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source);

  /**
   * Replaces the current ADS mux with a new one based on the given config.
   * Assumes that the given ads_config is syntactically valid (according to the PGV constraints).
   * @param ads_config an ADS config source to use.
   * @return the status of the operation.
   */
  absl::Status replaceAdsMux(const envoy::config::core::v3::ApiConfigSource& ads_config);

  Server::Instance& server_;
  Event::Dispatcher& main_thread_dispatcher_;
  Api::Api& api_;
  Random::RandomGenerator& random_;
  Stats::Store& stats_;
  const LocalInfo::LocalInfo& local_info_;
  ProtobufMessage::ValidationContext& validation_context_;
  XdsResourcesDelegatePtr xds_resources_delegate_;
  XdsConfigTrackerPtr xds_config_tracker_;
  std::unique_ptr<SubscriptionFactoryImpl> subscription_factory_;
  // The cm_ will only be valid after the cluster-manager is initialized.
  // Note that this implies that the xDS-manager must be shut down properly
  // prior to the cluster-manager deletion.
  Upstream::ClusterManager* cm_;
  GrpcMuxSharedPtr ads_mux_;

  // Stores all authorities as configured in the bootstrap.
  // Also includes the default config-source (the one that will be used for cases where the
  // authority in a resource doesn't exist, or doesn't match).
  std::vector<AuthorityData> authorities_;

  // The default authority that will be used for cases where the authority in a resource doesn't
  // exist, or doesn't match.
  AuthorityDataIterator default_authority_;
};

} // namespace Config
} // namespace Envoy
