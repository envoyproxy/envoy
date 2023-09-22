#include "validated_input_generator_any_map_extensions.h"

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/trace/v3/datadog.pb.h"

#include "source/extensions/access_loggers/file/config.h"
#include "source/extensions/http/early_header_mutation/header_mutation/config.h"
#include "source/extensions/http/header_validators/envoy_default/config.h"
#include "source/extensions/http/original_ip_detection/custom_header/config.h"
#include "source/extensions/http/original_ip_detection/xff/config.h"
#include "source/extensions/matching/actions/format_string/config.h"
#include "source/extensions/matching/common_inputs/environment_variable/config.h"
#include "source/extensions/matching/input_matchers/consistent_hashing/config.h"
#include "source/extensions/matching/input_matchers/ip/config.h"
#include "source/extensions/request_id/uuid/config.h"

namespace Envoy {
namespace ProtobufMessage {

ValidatedInputGenerator::AnyMap composeFiltersAnyMap() {
  static const auto dummy_proto_msg = []() -> std::unique_ptr<Protobuf::Message> {
    return std::make_unique<ProtobufWkt::Struct>();
  };

  static ValidatedInputGenerator::AnyMap any_map;
  if (any_map.empty()) {
    any_map = ValidatedInputGenerator::getDefaultAnyMap();
    any_map.insert(
        {{"envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager",
          {{"typed_header_validation_config",
            {{"envoy.extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::
                    HeaderValidatorFactoryConfig()
                        .createEmptyConfigProto();
              }}}},
           {"early_header_mutation_extensions",
            {{"envoy.extensions.http.early_header_mutation.header_mutation.v3.HeaderMutation",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::Http::EarlyHeaderMutation::HeaderMutation::Factory()
                    .createEmptyConfigProto();
              }}}},
           {"original_ip_detection_extensions",
            {{"envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::Http::OriginalIPDetection::CustomHeader::
                    CustomHeaderIPDetectionFactory()
                        .createEmptyConfigProto();
              }},
             {"envoy.extensions.http.original_ip_detection.xff.v3.XffConfig",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::Http::OriginalIPDetection::Xff::XffIPDetectionFactory()
                    .createEmptyConfigProto();
              }}}},
           {"access_log",
            {{"envoy.config.accesslog.v3.AccessLog.File",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::AccessLoggers::File::FileAccessLogFactory()
                    .createEmptyConfigProto();
              }}}},
           {"request_id_extension",
            {{"envoy.extensions.request_id.uuid.v3.UuidRequestIdConfig",
              []() -> std::unique_ptr<Protobuf::Message> {
                return Envoy::Extensions::RequestId::UUIDRequestIDExtensionFactory()
                    .createEmptyConfigProto();
              }}}}}},
         {"envoy.config.core.v3.SubstitutionFormatString",
          {{"formatters",
            {{"envoy.extensions.formatter.metadata.v3.Metadata", dummy_proto_msg},
             {"envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery",
              dummy_proto_msg}}}}},
         {"envoy.config.route.v3.RouteConfiguration",
          {{"cluster_specifier_plugins",
            {{"envoy.config.route.v3.ClusterSpecifierPlugin",
              []() -> std::unique_ptr<Protobuf::Message> {
                return std::make_unique<envoy::config::route::v3::ClusterSpecifierPlugin>();
              }}}},
           {"typed_per_filter_config", {{"envoy.config.route.v3.FilterConfig", dummy_proto_msg}}}}},
         {"envoy.config.trace.v3.Tracing.Http",
          {{"typed_config",
            {{"envoy.config.trace.v3.DatadogConfig", []() -> std::unique_ptr<Protobuf::Message> {
                return std::make_unique<envoy::config::trace::v3::DatadogConfig>();
              }}}}}}});
  }
  return any_map;
}

} // namespace ProtobufMessage
} // namespace Envoy
