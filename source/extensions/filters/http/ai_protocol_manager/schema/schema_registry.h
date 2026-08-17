#pragma once

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

using PerRouteProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute;

// Registry mapping route schema configurations to their corresponding PayloadSchema.
class SchemaRegistry {
public:
  static const PayloadSchema* getSchema(PerRouteProto::Schema schema);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
