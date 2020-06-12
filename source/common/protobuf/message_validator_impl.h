#pragma once

#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/stats.h"

#include "common/common/documentation_url.h"
#include "common/common/logger.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace ProtobufMessage {

class NullValidationVisitorImpl : public ValidationVisitor {
public:
  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view) override {}
  void onDeprecatedField(absl::string_view, bool) override {}

  // Envoy::ProtobufMessage::ValidationVisitor
  bool skipValidation() override { return true; }
};

ValidationVisitor& getNullValidationVisitor();

class WarningValidationVisitorImpl : public ValidationVisitor,
                                     public Logger::Loggable<Logger::Id::config> {
public:
  void setUnknownCounter(Stats::Counter& counter);

  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;

  // Envoy::ProtobufMessage::ValidationVisitor
  bool skipValidation() override { return false; }

private:
  // Track hashes of descriptions we've seen, to avoid log spam. A hash is used here to avoid
  // wasting memory with unused strings.
  absl::flat_hash_set<uint64_t> descriptions_;
  // This can be late initialized via setUnknownCounter(), enabling the server bootstrap loading
  // which occurs prior to the initialization of the stats subsystem.
  Stats::Counter* unknown_counter_{};
  uint64_t prestats_unknown_count_{};
};

class StrictValidationVisitorImpl : public ValidationVisitor {
public:
  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;

  // Envoy::ProtobufMessage::ValidationVisitor
  bool skipValidation() override { return false; }
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;
};

ValidationVisitor& getStrictValidationVisitor();

class ValidationContextImpl : public ValidationContext {
public:
  ValidationContextImpl(ValidationVisitor& static_validation_visitor,
                        ValidationVisitor& dynamic_validation_visitor)
      : static_validation_visitor_(static_validation_visitor),
        dynamic_validation_visitor_(dynamic_validation_visitor) {}

  // Envoy::ProtobufMessage::ValidationContext
  ValidationVisitor& staticValidationVisitor() override { return static_validation_visitor_; }
  ValidationVisitor& dynamicValidationVisitor() override { return dynamic_validation_visitor_; }

private:
  ValidationVisitor& static_validation_visitor_;
  ValidationVisitor& dynamic_validation_visitor_;
};

class ProdValidationContextImpl : public ValidationContextImpl {
public:
  ProdValidationContextImpl(bool allow_unknown_static_fields, bool allow_unknown_dynamic_fields,
                            bool ignore_unknown_dynamic_fields)
      : ValidationContextImpl(allow_unknown_static_fields ? static_warning_validation_visitor_
                                                          : getStrictValidationVisitor(),
                              allow_unknown_dynamic_fields
                                  ? (ignore_unknown_dynamic_fields
                                         ? ProtobufMessage::getNullValidationVisitor()
                                         : dynamic_warning_validation_visitor_)
                                  : ProtobufMessage::getStrictValidationVisitor()) {}

  ProtobufMessage::WarningValidationVisitorImpl& static_warning_validation_visitor() {
    return static_warning_validation_visitor_;
  }

  ProtobufMessage::WarningValidationVisitorImpl& dynamic_warning_validation_visitor() {
    return dynamic_warning_validation_visitor_;
  }

private:
  ProtobufMessage::WarningValidationVisitorImpl static_warning_validation_visitor_;
  ProtobufMessage::WarningValidationVisitorImpl dynamic_warning_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
