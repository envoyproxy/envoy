// Stub implementation of ValidatedInputGenerator after PGV removal.
// This class previously used PGV validation rules to generate constrained fuzz inputs.
// After migration to protovalidate, the C++ runtime validation is no longer available.
// This stub allows fuzz tests to compile but without PGV constraint enforcement.

#include "validated_input_generator.h"

#include "source/common/protobuf/visitor_helper.h"

#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/domain.pb.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"
#include "xds/type/matcher/v3/ip.pb.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/range.pb.h"
#include "xds/type/matcher/v3/regex.pb.h"
#include "xds/type/matcher/v3/string.pb.h"

namespace Envoy {
namespace ProtobufMessage {

const std::string ValidatedInputGenerator::kAny = "google.protobuf.Any";

ValidatedInputGenerator::ValidatedInputGenerator(unsigned int seed, AnyMap&& default_any_map,
                                                 unsigned int max_depth)
    : current_depth_(0), max_depth_(max_depth), any_map_(std::move(default_any_map)) {
  random_.initializeSeed(seed);
  mutator_.Seed(seed);
}

// Stub implementation - does not enforce PGV validation rules
void ValidatedInputGenerator::handleAnyRules(Protobuf::Message*,
                                             const absl::Span<const Protobuf::Message* const>&) {
  // Stub: Previously handled validate::AnyRules. No longer enforces PGV constraints.
}

void ValidatedInputGenerator::handleMessageTypedField(
    Protobuf::Message&, const Protobuf::FieldDescriptor&, const Protobuf::Reflection*,
    const absl::Span<const Protobuf::Message* const>&, bool, bool) {
  // Stub: Previously handled validate::FieldRules for message fields.
  // No longer enforces PGV constraints.
}

void ValidatedInputGenerator::onField(Protobuf::Message& msg,
                                      const Protobuf::FieldDescriptor& field,
                                      const absl::Span<const Protobuf::Message* const> parents) {
  onField(msg, field, parents, false, false);
}

void ValidatedInputGenerator::onField(Protobuf::Message&, const Protobuf::FieldDescriptor&,
                                      const absl::Span<const Protobuf::Message* const>, bool,
                                      bool) {
  // Stub: Previously extracted and enforced PGV validation rules.
  // No longer enforces PGV constraints.
}

void ValidatedInputGenerator::onEnterMessage(Protobuf::Message&,
                                             absl::Span<const Protobuf::Message* const>, bool,
                                             absl::string_view field_name) {
  message_path_.push_back(field_name);
  ++current_depth_;
}

void ValidatedInputGenerator::onLeaveMessage(Protobuf::Message&,
                                             absl::Span<const Protobuf::Message* const>, bool,
                                             absl::string_view) {
  message_path_.pop_back();
  current_depth_--;
}

ValidatedInputGenerator::AnyMap ValidatedInputGenerator::getDefaultAnyMap() {
  // Stub: Returns an empty map. Previously used PGV validation to constrain Any types.
  return AnyMap();
}

} // namespace ProtobufMessage
} // namespace Envoy
