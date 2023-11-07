#pragma once

#include "source/common/protobuf/protobuf.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)
namespace Envoy {
namespace DeterministicProtoHash {
uint64_t hash(const Protobuf::Message& message);
}
} // namespace Envoy
#endif
