#pragma once

#include <string>

#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExternalProcessingFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor> {

public:
  ExternalProcessingFilterConfig() : FactoryBase(HttpFilterNames::get().ExternalProcessing) {}

private:
  static constexpr uint64_t DefaultTimeout = 200;

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy