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

// Base class for both warning and strict validators.
class WipCounterBase {
protected:
  void setWipCounter(Stats::Counter& wip_counter);
  void onWorkInProgressCommon(absl::string_view description);

private:
  Stats::Counter* wip_counter_{};
  uint64_t prestats_wip_count_{};
};

class WarningValidationVisitorImpl : public ValidationVisitor,
                                     public WipCounterBase,
                                     public Logger::Loggable<Logger::Id::config> {
public:
  void setCounters(Stats::Counter& unknown_counter, Stats::Counter& wip_counter);

  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;
  bool skipValidation() override { return false; }
  void onWorkInProgress(absl::string_view description) override;

private:
  // Track hashes of descriptions we've seen, to avoid log spam. A hash is used here to avoid
  // wasting memory with unused strings.
  absl::flat_hash_set<uint64_t> descriptions_;
  // This can be late initialized via setUnknownCounter(), enabling the server bootstrap loading
  // which occurs prior to the initialization of the stats subsystem.
  Stats::Counter* unknown_counter_{};
  uint64_t prestats_unknown_count_{};
};

class StrictValidationVisitorImpl : public ValidationVisitor, public WipCounterBase {
public:
  void setCounters(Stats::Counter& wip_counter) { setWipCounter(wip_counter); }

  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
  bool skipValidation() override { return false; }
  void onDeprecatedField(absl::string_view description, bool soft_deprecation) override;
  void onWorkInProgress(absl::string_view description) override;
};

// TODO(mattklein123): There are various places where the default strict validator is being used.
// This does not increment the WIP stat. We should remover this as a public function and make sure
// that all code is either using the server validation context or the null validator.
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
      : ValidationContextImpl(
            allow_unknown_static_fields
                ? static_cast<ValidationVisitor&>(static_warning_validation_visitor_)
                : strict_validation_visitor_,
            allow_unknown_dynamic_fields
                ? (ignore_unknown_dynamic_fields ? ProtobufMessage::getNullValidationVisitor()
                                                 : dynamic_warning_validation_visitor_)
                : strict_validation_visitor_) {}

  void setCounters(Stats::Counter& static_unknown_counter, Stats::Counter& dynamic_unknown_counter,
                   Stats::Counter& wip_counter) {
    strict_validation_visitor_.setCounters(wip_counter);
    static_warning_validation_visitor_.setCounters(static_unknown_counter, wip_counter);
    dynamic_warning_validation_visitor_.setCounters(dynamic_unknown_counter, wip_counter);
  }

private:
  StrictValidationVisitorImpl strict_validation_visitor_;
  ProtobufMessage::WarningValidationVisitorImpl static_warning_validation_visitor_;
  ProtobufMessage::WarningValidationVisitorImpl dynamic_warning_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
