#include "common/http/connection_mapper_factory.h"

namespace Envoy {
namespace Http {

ConnectionMapperFactory::~ConnectionMapperFactory() = default;

ConnectionMapperPtr
ConnectionMapperFactory::createSrcTransparentMapper(const ConnPoolBuilder& /*unused*/) {
  return nullptr;
}

} // namespace Http
} // namespace Envoy
