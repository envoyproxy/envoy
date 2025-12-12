#pragma once

#include "envoy/server/factory_context.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Router {
class PerFilterConfigs : public Logger::Loggable<Logger::Id::http> {
public:
  static absl::StatusOr<std::unique_ptr<PerFilterConfigs>>
  create(const Protobuf::Map<std::string, ProtobufWkt::Any>& typed_configs,
         Server::Configuration::ServerFactoryContext& factory_context,
         ProtobufMessage::ValidationVisitor& validator);

  struct FilterConfig {
    RouteSpecificFilterConfigConstSharedPtr config_;
    bool disabled_{};
  };

  const RouteSpecificFilterConfig* get(absl::string_view name) const;

  /**
   * @return true if the filter is explicitly disabled for this route or virtual host, false
   * if the filter is explicitly enabled. If the filter is not explicitly enabled or disabled,
   * returns absl::nullopt.
   */
  absl::optional<bool> disabled(absl::string_view name) const;

private:
  PerFilterConfigs(const Protobuf::Map<std::string, ProtobufWkt::Any>& typed_configs,
                   Server::Configuration::ServerFactoryContext& factory_context,
                   ProtobufMessage::ValidationVisitor& validator, absl::Status& creation_status);

  absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                                  bool is_optional,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  ProtobufMessage::ValidationVisitor& validator);
  absl::flat_hash_map<std::string, FilterConfig> configs_;
};

} // namespace Router
} // namespace Envoy
