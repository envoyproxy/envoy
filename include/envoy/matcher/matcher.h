#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/typed_config.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "external/envoy_api/envoy/config/core/v3/extension.pb.h"

namespace Envoy {
namespace Matcher {

// This file describes a MatchTree<DataType>, which traverses a tree of matches until it
// either matches (resulting in either an action or a new tree to traverse) or doesn't match.
//
// All the matching is performed on strings: a DataInput<DataType> is used to extract a specific
// string from an instance of DataType, while an InputMatcher is used to determine whether the
// extracted string is a match.
//
// For example, DataType might be the type HttpDataInput, allowing
// for the use of HttpRequestHeaders : DataInput<HttpDataInput>, which is configured with the name
// of the header to extract from the request headers.
//
// In cases where the data to match on becomes available over time, this would be fed into the
// DataType over time, allowing matching to be re-attempted as more data is made available. As such
// whenever we extract data from a DataInput, we make note of whether the data might change and pause
// matching until we either match or have all the data.
template <class DataType> class MatchTree;

template <class DataType> using MatchTreeSharedPtr = std::shared_ptr<MatchTree<DataType>>;

using TypedExtensionConfigOpt = absl::optional<envoy::config::core::v3::TypedExtensionConfig>;

// On match, we either return the action to perform or another match tree to match against.
template <class DataType> struct OnMatch {
  const TypedExtensionConfigOpt action_;
  const MatchTreeSharedPtr<DataType> matcher_;
};

/**
 * MatchTree provides the interface for performing matches against the data provided by DataType.
 */
template <class DataType> class MatchTree {
public:
  virtual ~MatchTree() = default;

  // This encodes a three states:
  // Not enough data to complete the match: {false, {}}
  // Completed the match, no match: {true, {}}
  // Completed the match, match: {true, on_match}
  struct MatchResult {
    const bool match_completed_;
    const absl::optional<OnMatch<DataType>> on_match_;
  };

  // Attempts to match against the matching data (which should contain all the data requested via
  // matching requirements). If the match couldn't be completed, {false, {}} will be returned.
  // If a match result was determined, {true, action} will be returned. If a match result was
  // determined to be no match, {true, {}} will be returned.
  virtual MatchResult match(const DataType& matching_data) PURE;
};

// InputMatcher provides the interface for determining whether an input value matches.
class InputMatcher {
public:
  virtual ~InputMatcher() = default;

  /**
   * whether the provided input is a match.
   * @param absl::optional<absl::string_view> the value to match on. Will be absl::nullopt if the
   * lookup failed.
   */
  virtual bool match(absl::optional<absl::string_view> input) PURE;
};

using InputMatcherPtr = std::unique_ptr<InputMatcher>;

/**
 * Factory for registering custom input matchers.
 */
class InputMatcherFactory : public Config::TypedFactory {
public:
  virtual InputMatcherPtr createInputMatcher(Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.matching.matcher"; }
};

struct DataInputGetResult {
  // The data has not arrived yet, nothing to match against.
  bool not_available_yet;
  // Some data is available, so matching can be attempted. If it fails, more data might arrive which
  // could satisfy the match.
  // TODO(snowp): Should this use the "can change result" pattern of predicate? Should we ever go
  // from match to no match?
  bool more_data_available;
  // The data is available: result of looking up the data. If the lookup failed against partial or
  // complete data this will remain absl::nullopt.
  absl::optional<absl::string_view> data_;

  friend std::ostream& operator<<(std::ostream& out, const DataInputGetResult& result) {
    out << "data input:";
    out << "\nnot available: " << result.not_available_yet;
    out << "\nmore available: " << result.more_data_available;
    out << "\ndata: " << (result.data_ ? result.data_.value() : "n/a");
    return out;
  }
};

/**
 * Interface for types providing a data input extracted from a DataType.
 */
template <class DataType> class DataInput {
public:
  virtual ~DataInput() = default;

  virtual DataInputGetResult get(const DataType& data) PURE;
};

template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;

/**
 * Factory for data inputs.
 */
template <class DataType> class DataInputFactory : public Config::TypedFactory {
public:
  virtual DataInputPtr<DataType> createDataInput(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.matching.input"; }
};

} // namespace Matcher
} // namespace Envoy