#pragma once

#include "test/fuzz/validated_input_generator.h"

namespace Envoy {
namespace ProtobufMessage {

ValidatedInputGenerator::AnyMap compose_filters_any_map();

} // namespace ProtobufMessage
} // namespace Envoy
