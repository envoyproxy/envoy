#pragma once

#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/dynamic_secret_provider.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Secret {

/**
 * DynamicTlsCertificateSecretProviderFactoryContext passed to
 * DynamicTlsCertificateSecretProviderFactory to access resources which are needed for creating
 * dynamic tls certificate secret provider.
 */
class DynamicTlsCertificateSecretProviderFactoryContext {
public:
  virtual ~DynamicTlsCertificateSecretProviderFactoryContext() {}

  /**
   * @return information about the local environment the server is running in.
   */
  virtual const LocalInfo::LocalInfo& local_info() PURE;

  /**
   * @return Event::Dispatcher& the main thread's dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return RandomGenerator& the random generator for the server.
   */
  virtual Runtime::RandomGenerator& random() PURE;

  /**
   * @return the server-wide stats store.
   */
  virtual Stats::Store& stats() PURE;

  /**
   * @return Upstream::ClusterManager.
   */
  virtual Upstream::ClusterManager& cluster_manager() PURE;
};

/**
 * Factory for creating dynamic TlsCertificate secret provider.
 */
class DynamicTlsCertificateSecretProviderFactory {
public:
  virtual ~DynamicTlsCertificateSecretProviderFactory() {}

  /**
   * Finds and returns a secret provider associated to SDS config. Create a new one
   * if such provider does not exist.
   *
   * @param config_source a protobuf message object contains SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source.
   * @return the dynamic tls certificate secret provider.
   */
  virtual DynamicTlsCertificateSecretProviderSharedPtr
  findOrCreate(const envoy::api::v2::core::ConfigSource& sds_config,
               std::string sds_config_name) PURE;
};

} // namespace Secret
} // namespace Envoy