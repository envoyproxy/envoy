#pragma once

#include "source/common/protobuf/protobuf.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)
namespace Envoy {
namespace DeterministicProtoHash {

// Note: this ignores unknown fields and unrecognized types in Any fields.
// An alternative approach might treat such fields as "raw data" and include
// them in the hash, which would risk breaking the deterministic behavior,
// versus this way risks ignoring significant data.
//
// Ignoring unknown fields was chosen as the implementation because the
// TextFormat-based hashing this replaces was explicitly ignoring unknown
// fields.
//
// If this is used as part of making a hash table, it may result in
// collisions if unknown fields are present and are not ignored by the
// corresponding comparator. A `MessageDifferencer` can be configured to
// ignore unknown fields, or not to.
uint64_t hash(const Protobuf::Message& message);

} // namespace DeterministicProtoHash
} // namespace Envoy
#endif
