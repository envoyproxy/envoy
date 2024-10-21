#include "source/common/protobuf/utility.h"

#include <limits>
#include <numeric>

#include "envoy/annotations/deprecation.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/documentation_url.h"
#include "source/common/common/fmt.h"
#include "source/common/protobuf/deterministic_hash.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/visitor.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"
#include "udpa/annotations/sensitive.pb.h"
#include "udpa/annotations/status.pb.h"
#include "validate/validate.h"
#include "xds/annotations/v3/status.pb.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace {

// Validates that the max value of nanoseconds and seconds doesn't cause an
// overflow in the protobuf time-util computations.
// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should be renamed to validateDurationNoThrow.
absl::Status validateDurationUnifiedNoThrow(const ProtobufWkt::Duration& duration) {
  // Apply a strict max boundary to the `seconds` value to avoid overflow when
  // both seconds and nanoseconds are at their highest values.
  // Note that protobuf internally converts to the input's seconds and
  // nanoseconds to nanoseconds (with a max nanoseconds value of 999999999).
  // The kMaxSecondsValue = 9223372035, which is about 292 years.
  constexpr int64_t kMaxSecondsValue =
      (std::numeric_limits<int64_t>::max() - 999999999) / (1000 * 1000 * 1000);

  if (duration.seconds() < 0 || duration.nanos() < 0) {
    return absl::OutOfRangeError(
        fmt::format("Expected positive duration: {}", duration.DebugString()));
  }
  if (!Protobuf::util::TimeUtil::IsDurationValid(duration)) {
    return absl::OutOfRangeError(
        fmt::format("Duration out-of-range according to Protobuf: {}", duration.DebugString()));
  }
  if (duration.nanos() > 999999999 || duration.seconds() > kMaxSecondsValue) {
    return absl::OutOfRangeError(fmt::format("Duration out-of-range: {}", duration.DebugString()));
  }
  return absl::OkStatus();
}

// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should be removed.
absl::Status validateDurationNoThrow(const ProtobufWkt::Duration& duration,
                                     int64_t max_seconds_value) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
    return validateDurationUnifiedNoThrow(duration);
  }
  if (duration.seconds() < 0 || duration.nanos() < 0) {
    return absl::OutOfRangeError(
        fmt::format("Expected positive duration: {}", duration.DebugString()));
  }
  if (duration.nanos() > 999999999 || duration.seconds() > max_seconds_value) {
    return absl::OutOfRangeError(fmt::format("Duration out-of-range: {}", duration.DebugString()));
  }
  return absl::OkStatus();
}

// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should call validateDurationUnifiedNoThrow instead
// of validateDurationNoThrow.
void validateDuration(const ProtobufWkt::Duration& duration, int64_t max_seconds_value) {
  const auto result = validateDurationNoThrow(duration, max_seconds_value);
  if (!result.ok()) {
    throwEnvoyExceptionOrPanic(std::string(result.message()));
  }
}

// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should be removed.
void validateDuration(const ProtobufWkt::Duration& duration) {
  validateDuration(duration, Protobuf::util::TimeUtil::kDurationMaxSeconds);
}

// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should be removed.
void validateDurationAsMilliseconds(const ProtobufWkt::Duration& duration) {
  // Apply stricter max boundary to the `seconds` value to avoid overflow.
  // Note that protobuf internally converts to nanoseconds.
  // The kMaxInt64Nanoseconds = 9223372036, which is about 300 years.
  constexpr int64_t kMaxInt64Nanoseconds =
      std::numeric_limits<int64_t>::max() / (1000 * 1000 * 1000);
  validateDuration(duration, kMaxInt64Nanoseconds);
}

// TODO(adisuissa): Once "envoy.reloadable_features.strict_duration_validation"
// is removed this function should be removed.
absl::Status validateDurationAsMillisecondsNoThrow(const ProtobufWkt::Duration& duration) {
  constexpr int64_t kMaxInt64Nanoseconds =
      std::numeric_limits<int64_t>::max() / (1000 * 1000 * 1000);
  return validateDurationNoThrow(duration, kMaxInt64Nanoseconds);
}

} // namespace

namespace {

absl::string_view filenameFromPath(absl::string_view full_path) {
  size_t index = full_path.rfind('/');
  if (index == std::string::npos || index == full_path.size()) {
    return full_path;
  }
  return full_path.substr(index + 1, full_path.size());
}

// Logs a warning for use of a deprecated field or runtime-overridden use of an
// otherwise fatal field. Throws a warning on use of a fatal by default field.
void deprecatedFieldHelper(Runtime::Loader* runtime, bool proto_annotated_as_deprecated,
                           bool proto_annotated_as_disallowed, const std::string& feature_name,
                           std::string error, const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor) {
// This option is for Envoy builds with --define deprecated_features=disabled
// The build options CI then verifies that as Envoy developers deprecate fields,
// that they update canonical configs and unit tests to not use those deprecated
// fields, by making their use fatal in the build options CI.
#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
  bool warn_only = false;
#else
  bool warn_only = true;
#endif
  if (runtime &&
      runtime->snapshot().getBoolean("envoy.features.fail_on_any_deprecated_feature", false)) {
    warn_only = false;
  }
  bool warn_default = warn_only;
  // Allow runtime to be null both to not crash if this is called before server initialization,
  // and so proto validation works in context where runtime singleton is not set up (e.g.
  // standalone config validation utilities)
  if (runtime && proto_annotated_as_deprecated) {
    // This is set here, rather than above, so that in the absence of a
    // registry (i.e. test) the default for if a feature is allowed or not is
    // based on ENVOY_DISABLE_DEPRECATED_FEATURES.
    warn_only &= !proto_annotated_as_disallowed;
    warn_default = warn_only;
    warn_only = runtime->snapshot().deprecatedFeatureEnabled(feature_name, warn_only);
  }
  // Note this only checks if the runtime override has an actual effect. It
  // does not change the logged warning if someone "allows" a deprecated but not
  // yet fatal field.
  const bool runtime_overridden = (warn_default == false && warn_only == true);

  std::string with_overridden = fmt::format(
      fmt::runtime(error),
      (runtime_overridden ? "runtime overrides to continue using now fatal-by-default " : ""));

  THROW_IF_NOT_OK(validation_visitor.onDeprecatedField(
      "type " + message.GetTypeName() + " " + with_overridden, warn_only));
}

} // namespace

namespace ProtobufPercentHelper {

uint64_t checkAndReturnDefault(uint64_t default_value, uint64_t max_value) {
  ASSERT(default_value <= max_value);
  return default_value;
}

uint64_t convertPercent(double percent, uint64_t max_value) {
  // Checked by schema.
  ASSERT(percent >= 0.0 && percent <= 100.0);
  return max_value * (percent / 100.0);
}

bool evaluateFractionalPercent(envoy::type::v3::FractionalPercent percent, uint64_t random_value) {
  return random_value % fractionalPercentDenominatorToInt(percent.denominator()) <
         percent.numerator();
}

uint64_t fractionalPercentDenominatorToInt(
    const envoy::type::v3::FractionalPercent::DenominatorType& denominator) {
  switch (denominator) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::type::v3::FractionalPercent::HUNDRED:
    return 100;
  case envoy::type::v3::FractionalPercent::TEN_THOUSAND:
    return 10000;
  case envoy::type::v3::FractionalPercent::MILLION:
    return 1000000;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

} // namespace ProtobufPercentHelper

void ProtoExceptionUtil::throwMissingFieldException(const std::string& field_name,
                                                    const Protobuf::Message& message) {
  std::string error =
      fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString());
  throwEnvoyExceptionOrPanic(error);
}

void ProtoExceptionUtil::throwProtoValidationException(const std::string& validation_error,
                                                       const Protobuf::Message& message) {
  std::string error = fmt::format("Proto constraint validation failed ({}): {}", validation_error,
                                  message.DebugString());
  throwEnvoyExceptionOrPanic(error);
}

size_t MessageUtil::hash(const Protobuf::Message& message) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  return DeterministicProtoHash::hash(message);
#else
  return HashUtil::xxHash64(message.SerializeAsString());
#endif
}

#if !defined(ENVOY_ENABLE_FULL_PROTOS)
// NOLINTNEXTLINE(readability-identifier-naming)
bool MessageLiteDifferencer::Equals(const Protobuf::Message& message1,
                                    const Protobuf::Message& message2) {
  return MessageUtil::hash(message1) == MessageUtil::hash(message2);
}

// NOLINTNEXTLINE(readability-identifier-naming)
bool MessageLiteDifferencer::Equivalent(const Protobuf::Message& message1,
                                        const Protobuf::Message& message2) {
  return Equals(message1, message2);
}
#endif

namespace {

void checkForDeprecatedNonRepeatedEnumValue(
    const Protobuf::Message& message, absl::string_view filename,
    const Protobuf::FieldDescriptor* field, const Protobuf::Reflection* reflection,
    Runtime::Loader* runtime, ProtobufMessage::ValidationVisitor& validation_visitor) {
  // Repeated fields will be handled by recursion in checkForUnexpectedFields.
  if (field->is_repeated() || field->cpp_type() != Protobuf::FieldDescriptor::CPPTYPE_ENUM) {
    return;
  }

  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
  bool default_value = !reflection->HasField(*reflectable_message, field);

  const Protobuf::EnumValueDescriptor* enum_value_descriptor =
      reflection->GetEnum(*reflectable_message, field);
  if (!enum_value_descriptor->options().deprecated()) {
    return;
  }

  const std::string error =
      absl::StrCat("Using {}", (default_value ? "the default now-" : ""), "deprecated value ",
                   enum_value_descriptor->name(), " for enum '", field->full_name(), "' from file ",
                   filename, ". This enum value will be removed from Envoy soon",
                   (default_value ? " so a non-default value must now be explicitly set" : ""),
                   ". Please see " ENVOY_DOC_URL_VERSION_HISTORY " for details.");
  deprecatedFieldHelper(
      runtime, true /*deprecated*/,
      enum_value_descriptor->options().GetExtension(envoy::annotations::disallowed_by_default_enum),
      absl::StrCat("envoy.deprecated_features:", enum_value_descriptor->full_name()), error,
      message, validation_visitor);
}

constexpr absl::string_view WipWarning =
    "API features marked as work-in-progress are not considered stable, are not covered by the "
    "threat model, are not supported by the security team, and are subject to breaking changes. Do "
    "not use this feature without understanding each of the previous points.";

class UnexpectedFieldProtoVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  UnexpectedFieldProtoVisitor(ProtobufMessage::ValidationVisitor& validation_visitor,
                              Runtime::Loader* runtime)
      : validation_visitor_(validation_visitor), runtime_(runtime) {}

  void onField(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field) override {
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
    const Protobuf::Reflection* reflection = reflectable_message->GetReflection();
    absl::string_view filename = filenameFromPath(field.file()->name());

    // Before we check to see if the field is in use, see if there's a
    // deprecated default enum value.
    checkForDeprecatedNonRepeatedEnumValue(message, filename, &field, reflection, runtime_,
                                           validation_visitor_);

    // If this field is not in use, continue.
    if ((field.is_repeated() && reflection->FieldSize(*reflectable_message, &field) == 0) ||
        (!field.is_repeated() && !reflection->HasField(*reflectable_message, &field))) {
      return;
    }

    const auto& field_status = field.options().GetExtension(xds::annotations::v3::field_status);
    if (field_status.work_in_progress()) {
      validation_visitor_.onWorkInProgress(fmt::format(
          "field '{}' is marked as work-in-progress. {}", field.full_name(), WipWarning));
    }

    // If this field is deprecated, warn or throw an error.
    if (field.options().deprecated()) {
      const std::string warning =
          absl::StrCat("Using {}deprecated option '", field.full_name(), "' from file ", filename,
                       ". This configuration will be removed from "
                       "Envoy soon. Please see " ENVOY_DOC_URL_VERSION_HISTORY " for details.");

      deprecatedFieldHelper(runtime_, true /*deprecated*/,
                            field.options().GetExtension(envoy::annotations::disallowed_by_default),
                            absl::StrCat("envoy.deprecated_features:", field.full_name()), warning,
                            message, validation_visitor_);
    }
  }

  void onMessage(const Protobuf::Message& message,
                 absl::Span<const Protobuf::Message* const> parents, bool) override {
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
    if (reflectable_message->GetDescriptor()
            ->options()
            .GetExtension(xds::annotations::v3::message_status)
            .work_in_progress()) {
      validation_visitor_.onWorkInProgress(fmt::format(
          "message '{}' is marked as work-in-progress. {}", message.GetTypeName(), WipWarning));
    }

    const auto& udpa_file_options =
        reflectable_message->GetDescriptor()->file()->options().GetExtension(
            udpa::annotations::file_status);
    const auto& xds_file_options =
        reflectable_message->GetDescriptor()->file()->options().GetExtension(
            xds::annotations::v3::file_status);
    if (udpa_file_options.work_in_progress() || xds_file_options.work_in_progress()) {
      validation_visitor_.onWorkInProgress(fmt::format(
          "message '{}' is contained in proto file '{}' marked as work-in-progress. {}",
          message.GetTypeName(), reflectable_message->GetDescriptor()->file()->name(), WipWarning));
    }

    // Reject unknown fields.
    const auto& unknown_fields =
        reflectable_message->GetReflection()->GetUnknownFields(*reflectable_message);
    if (!unknown_fields.empty()) {
      std::string error_msg;
      for (int n = 0; n < unknown_fields.field_count(); ++n) {
        absl::StrAppend(&error_msg, n > 0 ? ", " : "", unknown_fields.field(n).number());
      }
      if (!error_msg.empty()) {
        THROW_IF_NOT_OK(validation_visitor_.onUnknownField(
            fmt::format("type {}({}) with unknown field set {{{}}}", message.GetTypeName(),
                        !parents.empty()
                            ? absl::StrJoin(parents, "::",
                                            [](std::string* out, const Protobuf::Message* const m) {
                                              absl::StrAppend(out, m->GetTypeName());
                                            })
                            : "root",
                        error_msg)));
      }
    }
  }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Runtime::Loader* runtime_;
};

} // namespace

void MessageUtil::checkForUnexpectedFields(const Protobuf::Message& message,
                                           ProtobufMessage::ValidationVisitor& validation_visitor,
                                           bool recurse_into_any) {
  Runtime::Loader* runtime = validation_visitor.runtime().has_value()
                                 ? &validation_visitor.runtime().value().get()
                                 : nullptr;
  UnexpectedFieldProtoVisitor unexpected_field_visitor(validation_visitor, runtime);
  THROW_IF_NOT_OK(
      ProtobufMessage::traverseMessage(unexpected_field_visitor, message, recurse_into_any));
}

namespace {

// A proto visitor that validates the correctness of google.protobuf.Duration messages
// as defined by Envoy's duration constraints.
class DurationFieldProtoVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  void onField(const Protobuf::Message&, const Protobuf::FieldDescriptor&) override {}

  void onMessage(const Protobuf::Message& message, absl::Span<const Protobuf::Message* const>,
                 bool) override {
    const Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
    if (reflectable_message->GetDescriptor()->full_name() == "google.protobuf.Duration") {
      ProtobufWkt::Duration duration_message;
#if defined(ENVOY_ENABLE_FULL_PROTOS)
      duration_message.CheckTypeAndMergeFrom(message);
#else
      duration_message.MergeFromCord(message.SerializeAsCord());
#endif
      // Validate the value of the duration.
      absl::Status status = validateDurationUnifiedNoThrow(duration_message);
      if (!status.ok()) {
        throwEnvoyExceptionOrPanic(fmt::format("Invalid duration: {}", status.message()));
      }
    }
  }
};

} // namespace

void MessageUtil::validateDurationFields(const Protobuf::Message& message, bool recurse_into_any) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_duration_validation")) {
    DurationFieldProtoVisitor duration_field_visitor;
    THROW_IF_NOT_OK(
        ProtobufMessage::traverseMessage(duration_field_visitor, message, recurse_into_any));
  }
}

namespace {

class PgvCheckVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  void onMessage(const Protobuf::Message& message, absl::Span<const Protobuf::Message* const>,
                 bool was_any_or_top_level) override {
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
    std::string err;
    // PGV verification is itself recursive up to the point at which it hits an Any message. As
    // such, to avoid N^2 checking of the tree, we only perform an additional check at the point
    // at which PGV would have stopped because it does not itself check within Any messages.
    if (was_any_or_top_level &&
        !pgv::BaseValidator::AbstractCheckMessage(*reflectable_message, &err)) {
      ProtoExceptionUtil::throwProtoValidationException(err, *reflectable_message);
    }
  }

  void onField(const Protobuf::Message&, const Protobuf::FieldDescriptor&) override {}
};

} // namespace

void MessageUtil::recursivePgvCheck(const Protobuf::Message& message) {
  PgvCheckVisitor visitor;
  THROW_IF_NOT_OK(ProtobufMessage::traverseMessage(visitor, message, true));
}

void MessageUtil::packFrom(ProtobufWkt::Any& any_message, const Protobuf::Message& message) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  any_message.PackFrom(message);
#else
  any_message.set_type_url(message.GetTypeName());
  any_message.set_value(message.SerializeAsString());
#endif
}

void MessageUtil::unpackToOrThrow(const ProtobufWkt::Any& any_message, Protobuf::Message& message) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  if (!any_message.UnpackTo(&message)) {
    throwEnvoyExceptionOrPanic(fmt::format("Unable to unpack as {}: {}",
                                           message.GetDescriptor()->full_name(),
                                           any_message.DebugString()));
#else
  if (!message.ParseFromString(any_message.value())) {
    throwEnvoyExceptionOrPanic(
        fmt::format("Unable to unpack as {}: {}", message.GetTypeName(), any_message.type_url()));
#endif
  }
}

absl::Status MessageUtil::unpackTo(const ProtobufWkt::Any& any_message,
                                   Protobuf::Message& message) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  if (!any_message.UnpackTo(&message)) {
    return absl::InternalError(absl::StrCat("Unable to unpack as ",
                                            message.GetDescriptor()->full_name(), ": ",
                                            any_message.DebugString()));
#else
  if (!message.ParseFromString(any_message.value())) {
    return absl::InternalError(
        absl::StrCat("Unable to unpack as ", message.GetTypeName(), ": ", any_message.type_url()));
#endif
  }
  // Ok Status is returned if `UnpackTo` succeeded.
  return absl::OkStatus();
}

std::string MessageUtil::convertToStringForLogs(const Protobuf::Message& message, bool pretty_print,
                                                bool always_print_primitive_fields) {
#ifdef ENVOY_ENABLE_YAML
  return getJsonStringFromMessageOrError(message, pretty_print, always_print_primitive_fields);
#else
  UNREFERENCED_PARAMETER(pretty_print);
  UNREFERENCED_PARAMETER(always_print_primitive_fields);
  return message.DebugString();
#endif
}

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::string& key, const std::string& value) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  val.set_string_value(value);
  (*struct_obj.mutable_fields())[key] = val;
  return struct_obj;
}

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::map<std::string, std::string>& fields) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  for (const auto& pair : fields) {
    val.set_string_value(pair.second);
    (*struct_obj.mutable_fields())[pair.first] = val;
  }
  return struct_obj;
}

std::string MessageUtil::codeEnumToString(absl::StatusCode code) {
  std::string result = absl::StatusCodeToString(code);
  // This preserves the behavior of the `ProtobufUtil::Status(code, "").ToString();`
  return !result.empty() ? result : "UNKNOWN: ";
}

namespace {

// Forward declaration for mutually-recursive helper functions.
void redact(Protobuf::Message* message, bool ancestor_is_sensitive);

using Transform = std::function<void(Protobuf::Message*, const Protobuf::Reflection*,
                                     const Protobuf::FieldDescriptor*)>;

// To redact opaque types, namely `Any` and `TypedStruct`, we have to reify them to the concrete
// message types specified by their `type_url` before we can redact their contents. This is mostly
// identical between `Any` and `TypedStruct`, the only difference being how they are packed and
// unpacked. Note that we have to use reflection on the opaque type here, rather than downcasting
// to `Any` or `TypedStruct`, because any message we might be handling could have originated from
// a `DynamicMessageFactory`.
bool redactOpaque(Protobuf::Message* message, bool ancestor_is_sensitive,
                  absl::string_view opaque_type_name, Transform unpack, Transform repack) {
  // Ensure this message has the opaque type we're expecting.
  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(*message);
  const auto* opaque_descriptor = reflectable_message->GetDescriptor();
  if (opaque_descriptor->full_name() != opaque_type_name) {
    return false;
  }

  // Find descriptors for the `type_url` and `value` fields. The `type_url` field must not be
  // empty, but `value` may be (in which case our work is done).
  const auto* reflection = reflectable_message->GetReflection();
  const auto* type_url_field_descriptor = opaque_descriptor->FindFieldByName("type_url");
  const auto* value_field_descriptor = opaque_descriptor->FindFieldByName("value");
  ASSERT(type_url_field_descriptor != nullptr && value_field_descriptor != nullptr);
  if (!reflection->HasField(*reflectable_message, type_url_field_descriptor) &&
      !reflection->HasField(*reflectable_message, value_field_descriptor)) {
    return true;
  }
  if (!reflection->HasField(*reflectable_message, type_url_field_descriptor) ||
      !reflection->HasField(*reflectable_message, value_field_descriptor)) {
    return false;
  }

  // Try to find a descriptor for `type_url` in the pool and instantiate a new message of the
  // correct concrete type.
  const std::string type_url(
      reflection->GetString(*reflectable_message, type_url_field_descriptor));
  const std::string concrete_type_name(TypeUtil::typeUrlToDescriptorFullName(type_url));
  const auto* concrete_descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(concrete_type_name);
  if (concrete_descriptor == nullptr) {
    // If the type URL doesn't correspond to a known proto, don't try to reify it, just treat it
    // like any other message. See the documented limitation on `MessageUtil::redact()` for more
    // context.
    ENVOY_LOG_MISC(warn, "Could not reify {} with unknown type URL {}", opaque_type_name, type_url);
    return false;
  }
  Protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<Protobuf::Message> typed_message(
      message_factory.GetPrototype(concrete_descriptor)->New());

  // Finally we can unpack, redact, and repack the opaque message using the provided callbacks.

  // Note: the content of opaque types may contain illegal content that mismatches the type_url
  // which may cause unpacking to fail. We catch the exception here to avoid crashing Envoy.
  TRY_ASSERT_MAIN_THREAD { unpack(typed_message.get(), reflection, value_field_descriptor); }
  END_TRY CATCH(const EnvoyException& e, {
    ENVOY_LOG_MISC(warn, "Could not unpack {} with type URL {}: {}", opaque_type_name, type_url,
                   e.what());
    return false;
  });
  redact(typed_message.get(), ancestor_is_sensitive);
  repack(typed_message.get(), reflection, value_field_descriptor);
  return true;
}

bool redactAny(Protobuf::Message* message, bool ancestor_is_sensitive) {
  return redactOpaque(
      message, ancestor_is_sensitive, "google.protobuf.Any",
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(*message);
        // To unpack an `Any`, parse the serialized proto.
        typed_message->ParseFromString(
            reflection->GetString(*reflectable_message, field_descriptor));
      },
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(*message);
        // To repack an `Any`, reserialize its proto.
        reflection->SetString(&(*reflectable_message), field_descriptor,
                              typed_message->SerializeAsString());
      });
}

// To redact a `TypedStruct`, we have to reify it based on its `type_url` to redact it.
bool redactTypedStruct(Protobuf::Message* message, const char* typed_struct_type,
                       bool ancestor_is_sensitive) {
  return redactOpaque(
      message, ancestor_is_sensitive, typed_struct_type,
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
#ifdef ENVOY_ENABLE_YAML
        // To unpack a `TypedStruct`, convert the struct from JSON.
        MessageUtil::jsonConvert(reflection->GetMessage(*message, field_descriptor),
                                 *typed_message);
#else
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(typed_message);
        UNREFERENCED_PARAMETER(reflection);
        UNREFERENCED_PARAMETER(field_descriptor);
        IS_ENVOY_BUG("redaction requested with JSON/YAML support removed");
#endif
      },
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
  // To repack a `TypedStruct`, convert the message back to JSON.
#ifdef ENVOY_ENABLE_YAML
        MessageUtil::jsonConvert(*typed_message,
                                 *(reflection->MutableMessage(message, field_descriptor)));
#else
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(typed_message);
        UNREFERENCED_PARAMETER(reflection);
        UNREFERENCED_PARAMETER(field_descriptor);
        IS_ENVOY_BUG("redaction requested with JSON/YAML support removed");
#endif
      });
}

// Recursive helper method for MessageUtil::redact() below.
void redact(Protobuf::Message* message, bool ancestor_is_sensitive) {
  if (redactAny(message, ancestor_is_sensitive) ||
      redactTypedStruct(message, "xds.type.v3.TypedStruct", ancestor_is_sensitive) ||
      redactTypedStruct(message, "udpa.type.v1.TypedStruct", ancestor_is_sensitive)) {
    return;
  }

  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(*message);
  const auto* descriptor = reflectable_message->GetDescriptor();
  const auto* reflection = reflectable_message->GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const auto* field_descriptor = descriptor->field(i);

    // Redact if this field or any of its ancestors have the `sensitive` option set.
    const bool sensitive = ancestor_is_sensitive ||
                           field_descriptor->options().GetExtension(udpa::annotations::sensitive);

    if (field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
      // Recursive case: traverse message fields.
      if (field_descriptor->is_map()) {
        // Redact values of maps only. Redacting both leaves the map with multiple "[redacted]"
        // keys.
        const int field_size = reflection->FieldSize(*reflectable_message, field_descriptor);
        for (int i = 0; i < field_size; ++i) {
          Protobuf::Message* map_pair_base =
              reflection->MutableRepeatedMessage(&(*reflectable_message), field_descriptor, i);
          Protobuf::ReflectableMessage map_pair = createReflectableMessage(*map_pair_base);
          auto* value_field_desc = map_pair->GetDescriptor()->FindFieldByName("value");
          if (sensitive && (value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_STRING ||
                            value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_BYTES)) {
            map_pair->GetReflection()->SetString(&(*map_pair), value_field_desc, "[redacted]");
          } else if (value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
            redact(map_pair->GetReflection()->MutableMessage(&(*map_pair), value_field_desc),
                   sensitive);
          } else if (sensitive) {
            map_pair->GetReflection()->ClearField(&(*map_pair), value_field_desc);
          }
        }
      } else if (field_descriptor->is_repeated()) {
        const int field_size = reflection->FieldSize(*reflectable_message, field_descriptor);
        for (int i = 0; i < field_size; ++i) {
          redact(reflection->MutableRepeatedMessage(&(*reflectable_message), field_descriptor, i),
                 sensitive);
        }
      } else if (reflection->HasField(*reflectable_message, field_descriptor)) {
        redact(reflection->MutableMessage(&(*reflectable_message), field_descriptor), sensitive);
      }
    } else if (sensitive) {
      // Base case: replace strings and bytes with "[redacted]" and clear all others.
      if (field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_STRING ||
          field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_BYTES) {
        if (field_descriptor->is_repeated()) {
          const int field_size = reflection->FieldSize(*reflectable_message, field_descriptor);
          for (int i = 0; i < field_size; ++i) {
            reflection->SetRepeatedString(&(*reflectable_message), field_descriptor, i,
                                          "[redacted]");
          }
        } else if (reflection->HasField(*reflectable_message, field_descriptor)) {
          reflection->SetString(&(*reflectable_message), field_descriptor, "[redacted]");
        }
      } else {
        reflection->ClearField(&(*reflectable_message), field_descriptor);
      }
    }
  }
}

} // namespace

void MessageUtil::redact(Protobuf::Message& message) {
  ::Envoy::redact(&message, /* ancestor_is_sensitive = */ false);
}

void MessageUtil::wireCast(const Protobuf::Message& src, Protobuf::Message& dst) {
  // This should should generally succeed, but if there are malformed UTF-8 strings in a message,
  // this can fail.
  if (!dst.ParseFromString(src.SerializeAsString())) {
    throwEnvoyExceptionOrPanic("Unable to deserialize during wireCast()");
  }
}

std::string MessageUtil::toTextProto(const Protobuf::Message& message) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  std::string text_format;
  Protobuf::TextFormat::Printer printer;
  printer.SetExpandAny(true);
  printer.SetHideUnknownFields(true);
  bool result = printer.PrintToString(message, &text_format);
  ASSERT(result);
  return text_format;
#else
  // Note that MessageLite::DebugString never had guarantees of producing
  // serializable text proto representation.
  return message.DebugString();
#endif
}

bool ValueUtil::equal(const ProtobufWkt::Value& v1, const ProtobufWkt::Value& v2) {
  ProtobufWkt::Value::KindCase kind = v1.kind_case();
  if (kind != v2.kind_case()) {
    return false;
  }

  switch (kind) {
  case ProtobufWkt::Value::KIND_NOT_SET:
    return v2.kind_case() == ProtobufWkt::Value::KIND_NOT_SET;

  case ProtobufWkt::Value::kNullValue:
    return true;

  case ProtobufWkt::Value::kNumberValue:
    return v1.number_value() == v2.number_value();

  case ProtobufWkt::Value::kStringValue:
    return v1.string_value() == v2.string_value();

  case ProtobufWkt::Value::kBoolValue:
    return v1.bool_value() == v2.bool_value();

  case ProtobufWkt::Value::kStructValue: {
    const ProtobufWkt::Struct& s1 = v1.struct_value();
    const ProtobufWkt::Struct& s2 = v2.struct_value();
    if (s1.fields_size() != s2.fields_size()) {
      return false;
    }
    for (const auto& it1 : s1.fields()) {
      const auto& it2 = s2.fields().find(it1.first);
      if (it2 == s2.fields().end()) {
        return false;
      }

      if (!equal(it1.second, it2->second)) {
        return false;
      }
    }
    return true;
  }

  case ProtobufWkt::Value::kListValue: {
    const ProtobufWkt::ListValue& l1 = v1.list_value();
    const ProtobufWkt::ListValue& l2 = v2.list_value();
    if (l1.values_size() != l2.values_size()) {
      return false;
    }
    for (int i = 0; i < l1.values_size(); i++) {
      if (!equal(l1.values(i), l2.values(i))) {
        return false;
      }
    }
    return true;
  }
  }
  return false;
}

const ProtobufWkt::Value& ValueUtil::nullValue() {
  static const auto* v = []() -> ProtobufWkt::Value* {
    auto* vv = new ProtobufWkt::Value();
    vv->set_null_value(ProtobufWkt::NULL_VALUE);
    return vv;
  }();
  return *v;
}

ProtobufWkt::Value ValueUtil::stringValue(const std::string& str) {
  ProtobufWkt::Value val;
  val.set_string_value(str);
  return val;
}

ProtobufWkt::Value ValueUtil::optionalStringValue(const absl::optional<std::string>& str) {
  if (str.has_value()) {
    return ValueUtil::stringValue(str.value());
  }
  return ValueUtil::nullValue();
}

ProtobufWkt::Value ValueUtil::boolValue(bool b) {
  ProtobufWkt::Value val;
  val.set_bool_value(b);
  return val;
}

ProtobufWkt::Value ValueUtil::structValue(const ProtobufWkt::Struct& obj) {
  ProtobufWkt::Value val;
  (*val.mutable_struct_value()) = obj;
  return val;
}

ProtobufWkt::Value ValueUtil::listValue(const std::vector<ProtobufWkt::Value>& values) {
  auto list = std::make_unique<ProtobufWkt::ListValue>();
  for (const auto& value : values) {
    *list->add_values() = value;
  }
  ProtobufWkt::Value val;
  val.set_allocated_list_value(list.release());
  return val;
}

uint64_t DurationUtil::durationToMilliseconds(const ProtobufWkt::Duration& duration) {
  validateDurationAsMilliseconds(duration);
  return Protobuf::util::TimeUtil::DurationToMilliseconds(duration);
}

absl::StatusOr<uint64_t>
DurationUtil::durationToMillisecondsNoThrow(const ProtobufWkt::Duration& duration) {
  const auto result = validateDurationAsMillisecondsNoThrow(duration);
  if (!result.ok()) {
    return result;
  }
  return Protobuf::util::TimeUtil::DurationToMilliseconds(duration);
}

uint64_t DurationUtil::durationToSeconds(const ProtobufWkt::Duration& duration) {
  validateDuration(duration);
  return Protobuf::util::TimeUtil::DurationToSeconds(duration);
}

void TimestampUtil::systemClockToTimestamp(const SystemTime system_clock_time,
                                           ProtobufWkt::Timestamp& timestamp) {
  // Converts to millisecond-precision Timestamp by explicitly casting to millisecond-precision
  // time_point.
  timestamp.MergeFrom(Protobuf::util::TimeUtil::MillisecondsToTimestamp(
      std::chrono::time_point_cast<std::chrono::milliseconds>(system_clock_time)
          .time_since_epoch()
          .count()));
}

absl::string_view TypeUtil::typeUrlToDescriptorFullName(absl::string_view type_url) {
  const size_t pos = type_url.rfind('/');
  if (pos != absl::string_view::npos) {
    type_url = type_url.substr(pos + 1);
  }
  return type_url;
}

std::string TypeUtil::descriptorFullNameToTypeUrl(absl::string_view type) {
  return "type.googleapis.com/" + std::string(type);
}

void StructUtil::update(ProtobufWkt::Struct& obj, const ProtobufWkt::Struct& with) {
  auto& obj_fields = *obj.mutable_fields();

  for (const auto& [key, val] : with.fields()) {
    auto& obj_key = obj_fields[key];

    // If the types are different, the last one wins.
    const auto val_kind = val.kind_case();
    if (val_kind != obj_key.kind_case()) {
      obj_key = val;
      continue;
    }

    // Otherwise, the strategy depends on the value kind.
    switch (val.kind_case()) {
    // For scalars, the last one wins.
    case ProtobufWkt::Value::kNullValue:
    case ProtobufWkt::Value::kNumberValue:
    case ProtobufWkt::Value::kStringValue:
    case ProtobufWkt::Value::kBoolValue:
      obj_key = val;
      break;
    // If we got a structure, recursively update.
    case ProtobufWkt::Value::kStructValue:
      update(*obj_key.mutable_struct_value(), val.struct_value());
      break;
    // For lists, append the new values.
    case ProtobufWkt::Value::kListValue: {
      auto& obj_key_vec = *obj_key.mutable_list_value()->mutable_values();
      const auto& vals = val.list_value().values();
      obj_key_vec.MergeFrom(vals);
      break;
    }
    case ProtobufWkt::Value::KIND_NOT_SET:
      break;
    }
  }
}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Api::Api& api) {
  auto file_or_error = api.fileSystem().fileReadToEnd(path);
  THROW_IF_NOT_OK_REF(file_or_error.status());
  const std::string contents = file_or_error.value();
  // If the filename ends with .pb, attempt to parse it as a binary proto.
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().ProtoBinary)) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      MessageUtil::checkForUnexpectedFields(message, validation_visitor);
    }
    // Ideally this would throw an error if ParseFromString fails for consistency
    // but instead it will silently fail.
    return;
  }

  // If the filename ends with .pb_text, attempt to parse it as a text proto.
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().ProtoText)) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
      return;
    }
#endif
    throwEnvoyExceptionOrPanic("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                               message.GetTypeName() + ")");
  }
#ifdef ENVOY_ENABLE_YAML
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().Yaml) ||
      absl::EndsWithIgnoreCase(path, FileExtensions::get().Yml)) {
    // loadFromYaml throws an error if parsing fails.
    loadFromYaml(contents, message, validation_visitor);
  } else {
    // loadFromJson does not consistently trow an error if parsing fails.
    // Ideally we would handle that case here.
    loadFromJson(contents, message, validation_visitor);
  }
#else
  throwEnvoyExceptionOrPanic("Unable to parse file \"" + path + "\" (type " +
                             message.GetTypeName() + ")");
#endif
}

} // namespace Envoy
