#include "common/http/wrapped/connection_mapper_factory.h"

#include "common/http/wrapped/src_ip_transparent_mapper.h"

namespace Envoy {
namespace Http {

namespace {
constexpr size_t DEFAULT_MAX_POOLS = 100;
}

ConnectionMapperFactory::~ConnectionMapperFactory() = default;

ConnectionMapperPtr
ConnectionMapperFactory::createSrcTransparentMapper(const ConnPoolBuilder& builder) {
  return std::make_unique<SrcIpTransparentMapper>(builder, DEFAULT_MAX_POOLS);
}

} // namespace Http
} // namespace Envoy
