#pragma once

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace ProtoFiller {

// The message packed into an Any, keyed by that Any field's name.
using AnyTypes = absl::flat_hash_map<std::string, const Protobuf::Message*>;

struct Options {
  // How many elements a repeated field gets, and how many entries a map gets.
  uint32_t elements{3};
  // How many levels of nested messages are filled.
  uint32_t max_depth{5};
  // Each Any is packed with its own instance of the mapped message. An Any whose field is unmapped
  // is left empty, since no type can be inferred.
  AnyTypes any_types;
};

/**
 * Sets every field of `message` to a non-default value, so a test can build a message the size of
 * a real one without naming its fields. Values derive from each field's name and number, so two
 * messages of the same type fill alike.
 *
 * Only one field of each oneof is set, as the others would overwrite it.
 */
void fill(Protobuf::Message& message, const Options& options = {});

} // namespace ProtoFiller
} // namespace Envoy
