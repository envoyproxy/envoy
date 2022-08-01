#pragma once

#include "envoy/config/config_validator.h"
#include "envoy/extensions/config/validators/minimum_clusters/v3/minimum_clusters.pb.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {

/**
 * A config validator extension that validates that the number of clusters do
 * not decrease below some threshold.
 */
class MinimumClustersValidator : public Envoy::Config::ConfigValidator {
public:
  MinimumClustersValidator(
      const envoy::extensions::config::validators::minimum_clusters::v3::MinimumClustersValidator&
          config)
      : min_clusters_num_(config.min_clusters_num()) {}

  // ConfigValidator
  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& resources) override;

  void validate(const Server::Instance& server,
                const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
                const Protobuf::RepeatedPtrField<std::string>& removed_resources) override;

private:
  const uint64_t min_clusters_num_;
};

} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
