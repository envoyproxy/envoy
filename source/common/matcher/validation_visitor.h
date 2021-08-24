#pragma once

#include "envoy/matcher/matcher.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Matcher {

/**
 * Interface for a validator that is used during match tree construction to validate
 * the paths, nodes, subtrees, etc. of the match tree. Currently only node-based (i.e. without
 * knowledge of the resulting leaf action) validation of data input is supported.
 *
 * As the match tree itself can have any structure, this class is used to accumulate a list of
 * violations that occur during tree construction. This has the benefit of being able to emit
 * an error containing all the violations, not just the first one.
 */
template <class DataType> class MatchTreeValidationVisitor {
public:
  virtual ~MatchTreeValidationVisitor() = default;

  // Validates a single DataInput its type_url.
  void validateDataInput(const DataInputFactory<DataType>& data_input, absl::string_view type_url) {
    auto status = performDataInputValidation(data_input, type_url);

    if (!status.ok()) {
      errors_.emplace_back(std::move(status));
    }
  }

  const std::vector<absl::Status>& errors() const { return errors_; }

protected:
  // Implementations would subclass this to specify the validation logic for data inputs,
  // returning a helpful error message if validation fails.
  virtual absl::Status performDataInputValidation(const DataInputFactory<DataType>& data_input,
                                                  absl::string_view type_url) PURE;

private:
  std::vector<absl::Status> errors_;
};
} // namespace Matcher
} // namespace Envoy
