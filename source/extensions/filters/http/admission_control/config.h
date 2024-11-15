#pragma once

#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/**
 * Config registration for the adaptive concurrency limit filter. @see NamedHttpFilterConfigFactory.
 */
class AdmissionControlFilterFactory
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::admission_control::v3::AdmissionControl> {
public:
  AdmissionControlFilterFactory() : DualFactoryBase("envoy.filters.http.admission_control") {}

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::admission_control::v3::AdmissionControl& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;
};

using UpstreamAdmissionControlFilterFactory = AdmissionControlFilterFactory;

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
