#pragma once

#include <string>

#include "envoy/config/filter/http/adaptive_concurrency_limit/v2alpha/adaptive_concurrency_limit.pb.validate.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Limit {

template <typename ConfigProto, typename Data> class FactoryBase {
public:
  virtual ~FactoryBase() {}
  std::unique_ptr<Upstream::Limit<Data>>
  createLimit(const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
                  AdaptiveConcurrencyLimit::Limit& config,
              Runtime::RandomGenerator& random, const std::string& cluster_name) {
    MessageUtil::validate<const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
                              AdaptiveConcurrencyLimit::Limit::CommonConfig&>(
        config.common_config());

    ConfigProto limit_specific_config;
    MessageUtil::jsonConvert(config.limit_specific_config(), limit_specific_config);
    MessageUtil::validate<const ConfigProto&>(limit_specific_config);

    return createLimitFromProtoTyped(config.common_config(), limit_specific_config, random,
                                     cluster_name);
  }

  std::string name() { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual std::unique_ptr<Upstream::Limit<Data>>
  createLimitFromProtoTyped(const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
                                AdaptiveConcurrencyLimit::Limit::CommonConfig& common_config,
                            const ConfigProto& limit_specific_config,
                            Runtime::RandomGenerator& random, const std::string& cluster_name) PURE;

  const std::string name_;
};

} // namespace Limit
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
