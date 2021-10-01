#pragma once

#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/stats.h"

#include "source/common/common/documentation_url.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace ProtobufMessage {

class NullValidationVisitorImpl : public ValidationVisitor {
public:
  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view) override {}
  void onDeprecatedField(absl::string_view, bool) override {}
  bool skipValidation() override { return true; }
  void onWorkInProgress(absl::string_view) override {}
};

ValidationVisitor& getNullValidationVisitor();

class WarningValidationVisitorImpl : public ValidationVisitor,
                                     public Logger::Loggable<Logger::Id::config> {
public:
  WarningValidationVisitorImpl(Stats::Counter& unknown_counter, Stats::Counter& wip_counter)
      : unknown_counter_(unknown_counter), wip_counter_(wip_counter) {}

  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;
  bool skipValidation() override { return false; }
  void onWorkInProgress(absl::string_view description) override;

private:
  // Track hashes of descriptions we've seen, to avoid log spam. A hash is used here to avoid
  // wasting memory with unused strings.
  absl::flat_hash_set<uint64_t> descriptions_;
  Stats::Counter& unknown_counter_;
  Stats::Counter& wip_counter_;
};

class StrictValidationVisitorImpl : public ValidationVisitor {
public:
  StrictValidationVisitorImpl() = default;
  StrictValidationVisitorImpl(Stats::Counter& wip_counter) : wip_counter_(&wip_counter) {}

  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
  bool skipValidation() override { return false; }
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;
  void onWorkInProgress(absl::string_view description) override;

private:
  Stats::Counter* wip_counter_{};
};

// fixfix todo
ValidationVisitor& getStrictValidationVisitor();

class ProdValidationContextImpl : public ValidationContext {
public:
  ProdValidationContextImpl(bool allow_unknown_static_fields, bool allow_unknown_dynamic_fields,
                            bool ignore_unknown_dynamic_fields,
                            Stats::Counter& static_unknown_counter,
                            Stats::Counter& dynamic_unknown_counter, Stats::Counter& wip_counter)
      : allow_unknown_static_fields_(allow_unknown_static_fields),
        allow_unknown_dynamic_fields_(allow_unknown_dynamic_fields),
        ignore_unknown_dynamic_fields_(ignore_unknown_dynamic_fields),
        strict_validation_visitor_(wip_counter),
        static_warning_validation_visitor_(static_unknown_counter, wip_counter),
        dynamic_warning_validation_visitor_(dynamic_unknown_counter, wip_counter) {}

  // Envoy::ProtobufMessage::ValidationContext
  ValidationVisitor& staticValidationVisitor() override {
    return allow_unknown_static_fields_ ? static_warning_validation_visitor_
                                        : strict_validation_visitor_;
  }
  ValidationVisitor& dynamicValidationVisitor() override {
    return allow_unknown_dynamic_fields_
               ? (ignore_unknown_dynamic_fields_ ? ProtobufMessage::getNullValidationVisitor()
                                                 : dynamic_warning_validation_visitor_)
               : strict_validation_visitor_;
  }

private:
  const bool allow_unknown_static_fields_;
  const bool allow_unknown_dynamic_fields_;
  const bool ignore_unknown_dynamic_fields_;
  StrictValidationVisitorImpl strict_validation_visitor_;
  ProtobufMessage::WarningValidationVisitorImpl static_warning_validation_visitor_;
  ProtobufMessage::WarningValidationVisitorImpl dynamic_warning_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
