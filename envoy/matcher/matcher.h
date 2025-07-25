#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class ServerFactoryContext;
}
} // namespace Server

namespace Matcher {

// Abstract interface for custom matching data.
// Overrides this interface to provide custom matcher specific implementation.
class CustomMatchData {
public:
  virtual ~CustomMatchData() = default;
};

using MatchingDataType =
    absl::variant<absl::monostate, std::string, std::shared_ptr<CustomMatchData>>;

inline constexpr absl::string_view DefaultMatchingDataType = "string";

// This file describes a MatchTree<DataType>, which traverses a tree of matches until it
// either matches (resulting in either an action or a new tree to traverse) or doesn't match.
// The matching might stop early if either the data is not available at all yet, or if more data
// might result in a match.

// By returning a new tree when an OnMatch results in a new tree, matching can be resumed from
// this tree should more data be required to complete matching. This avoids having to start
// from the beginning every time. At some point we might support resuming for any node in the match
// tree: this requires some careful handling of tracking which on_no_match to use should we fail to
// match.
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
// whenever we extract data from a DataInput, we make note of whether the data might change and
// pause matching until we either match or have all the data. It would then fall on the caller to
// both provide more information to the DataType and to resume matching.
template <class DataType> class MatchTree;

template <class DataType> using MatchTreeSharedPtr = std::shared_ptr<MatchTree<DataType>>;
template <class DataType> using MatchTreePtr = std::unique_ptr<MatchTree<DataType>>;
template <class DataType> using MatchTreeFactoryCb = std::function<MatchTreePtr<DataType>()>;

/**
 * Action provides the interface for actions to perform when a match occurs. It provides no
 * functions, as implementors are expected to downcast this to a more specific action.
 */
class Action {
public:
  virtual ~Action() = default;

  /**
   * The underlying type of this action. This can be used to determine which
   * type this action is before attempting to cast it.
   */
  virtual absl::string_view typeUrl() const PURE;

  /**
   * Helper to convert this action to its underlying type.
   */
  template <class T> const T& getTyped() const {
    ASSERT(dynamic_cast<const T*>(this) != nullptr);
    return static_cast<const T&>(*this);
  }
};

using ActionConstSharedPtr = std::shared_ptr<const Action>;

template <class ActionFactoryContext> class ActionFactory : public Config::TypedFactory {
public:
  virtual ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionFactoryContext& action_factory_context,
               ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.matching.action"; }
};

// On match, we either return the action to perform or another match tree to match against.
template <class DataType> struct OnMatch {
  const ActionConstSharedPtr action_;
  const MatchTreeSharedPtr<DataType> matcher_;
  bool keep_matching_{};
};
template <class DataType> using OnMatchFactoryCb = std::function<OnMatch<DataType>()>;

template <class DataType> class OnMatchFactory {
public:
  virtual ~OnMatchFactory() = default;

  // Instantiates a nested matcher sub-tree or an action.
  // Returns absl::nullopt if neither sub-tree or action is specified.
  virtual absl::optional<OnMatchFactoryCb<DataType>>
  createOnMatch(const xds::type::matcher::v3::Matcher::OnMatch&) PURE;
  // Instantiates a nested matcher sub-tree or an action.
  // Returns absl::nullopt if neither sub-tree or action is specified.
  virtual absl::optional<OnMatchFactoryCb<DataType>>
  createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch&) PURE;
};

// The result of a match. There are three possible results:
// - The match could not be completed due to lack of data (isInsufficientData() will return true.)
// - The match was completed, no match found (isNoMatch() will return true.)
// - The match was completed, match found (isMatch() will return true, action() will return the
//   ActionConstSharedPtr.)
struct MatchResult {
public:
  MatchResult(ActionConstSharedPtr cb) : result_(std::move(cb)) {}
  static MatchResult noMatch() { return MatchResult(NoMatch{}); }
  static MatchResult insufficientData() { return MatchResult(InsufficientData{}); }
  bool isInsufficientData() const { return absl::holds_alternative<InsufficientData>(result_); }
  bool isComplete() const { return !isInsufficientData(); }
  bool isNoMatch() const { return absl::holds_alternative<NoMatch>(result_); }
  bool isMatch() const { return absl::holds_alternative<ActionConstSharedPtr>(result_); }
  const ActionConstSharedPtr& action() const {
    ASSERT(isMatch());
    return absl::get<ActionConstSharedPtr>(result_);
  }
  // Returns the action by move. The caller must ensure that the MatchResult is not used after
  // this call.
  ActionConstSharedPtr actionByMove() {
    ASSERT(isMatch());
    return absl::get<ActionConstSharedPtr>(std::move(result_));
  }

private:
  struct InsufficientData {};
  struct NoMatch {};
  using Result = absl::variant<ActionConstSharedPtr, NoMatch, InsufficientData>;
  Result result_;
  MatchResult(NoMatch) : result_(NoMatch{}) {}
  MatchResult(InsufficientData) : result_(InsufficientData{}) {}
};

// Callback to execute against skipped matches' actions.
using SkippedMatchCb = std::function<void(const ActionConstSharedPtr&)>;

/**
 * MatchTree provides the interface for performing matches against the data provided by DataType.
 */
template <class DataType> class MatchTree {
public:
  virtual ~MatchTree() = default;

  // Attempts to match against the matching data (which should contain all the data requested via
  // matching requirements).
  // If the match couldn't be completed, MatchResult::insufficientData() will be returned.
  // If a match result was determined, an action callback factory will be returned.
  // If it was determined to be no match, MatchResult::noMatch() will be returned.
  //
  // Implementors should call handleRecursionAndSkips() to transform OnMatch values
  // into MatchResult values, and handle noMatch and insufficientData results as appropriate
  // for the specific matcher type.
  virtual MatchResult match(const DataType& matching_data,
                            SkippedMatchCb skipped_match_cb = nullptr) PURE;

protected:
  // Internally handle recursion & keep_matching logic in matcher implementations.
  // This should be called against initial matching & on-no-match results.
  static inline MatchResult
  handleRecursionAndSkips(const absl::optional<OnMatch<DataType>>& on_match, const DataType& data,
                          SkippedMatchCb skipped_match_cb) {
    if (!on_match.has_value()) {
      return MatchResult::noMatch();
    }
    if (on_match->matcher_) {
      MatchResult nested_result = on_match->matcher_->match(data, skipped_match_cb);
      // Parent result's keep_matching skips the nested result.
      if (on_match->keep_matching_ && nested_result.isMatch()) {
        if (skipped_match_cb) {
          skipped_match_cb(nested_result.action());
        }
        return MatchResult::noMatch();
      }
      return nested_result;
    }
    if (on_match->action_ && on_match->keep_matching_) {
      if (skipped_match_cb) {
        skipped_match_cb(on_match->action_);
      }
      return MatchResult::noMatch();
    }
    return MatchResult{on_match->action_};
  }
};

template <class DataType> using MatchTreeSharedPtr = std::shared_ptr<MatchTree<DataType>>;

// InputMatcher provides the interface for determining whether an input value matches.
class InputMatcher {
public:
  virtual ~InputMatcher() = default;

  /**
   * Whether the provided input is a match.
   * @param Matcher::MatchingDataType the value to match on. Will be absl::monostate() if the
   * lookup failed.
   */
  virtual bool match(const Matcher::MatchingDataType& input) PURE;

  /**
   * A set of data input types supported by InputMatcher.
   * String is default supported data input type because almost all the derived objects support
   * string only. The name of core types (e.g., std::string, int) is defined string constrant which
   * produces human-readable form (e.g., "string", "int").
   *
   * Override this function to provide matcher specific supported data input types.
   */
  virtual absl::flat_hash_set<std::string> supportedDataInputTypes() const {
    return absl::flat_hash_set<std::string>{std::string(DefaultMatchingDataType)};
  }
};

using InputMatcherPtr = std::unique_ptr<InputMatcher>;
using InputMatcherFactoryCb = std::function<InputMatcherPtr()>;

/**
 * Factory for registering custom input matchers.
 */
class InputMatcherFactory : public Config::TypedFactory {
public:
  virtual InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message& config,
                              Server::Configuration::ServerFactoryContext& factory_context) PURE;

  std::string category() const override { return "envoy.matching.input_matchers"; }
};

// The result of retrieving data from a DataInput. As the data is generally made available
// over time (e.g. as more of the stream reaches the proxy), data might become increasingly
// available. This return type allows the DataInput to indicate this, as this might influence
// the match decision.
//
// Conceptually the data availability should start at being NotAvailable, transition to
// MoreDataMightBeAvailable (optional, this doesn't make sense for all data) and finally
// AllDataAvailable as the data becomes available.
struct DataInputGetResult {
  enum class DataAvailability {
    // The data is not yet available.
    NotAvailable,
    // Some data is available, but more might arrive.
    MoreDataMightBeAvailable,
    // All the data is available.
    AllDataAvailable
  };

  DataAvailability data_availability_;
  // The resulting data. This will be absl::monostate() if we don't have sufficient data available
  // (as per data_availability_) or because no value was extracted. For example, consider a
  // DataInput which attempts to look a key up in the map: if we don't have access to the map yet,
  // we return absl::monostate() with NotAvailable. If we have the entire map, but the key doesn't
  // exist in the map, we return absl::monostate() with AllDataAvailable.
  MatchingDataType data_;

  // For pretty printing.
  friend std::ostream& operator<<(std::ostream& out, const DataInputGetResult& result) {
    out << "data input: "
        << (absl::holds_alternative<std::string>(result.data_)
                ? absl::get<std::string>(result.data_)
                : "n/a");

    switch (result.data_availability_) {
    case DataInputGetResult::DataAvailability::NotAvailable:
      out << " (not available)";
      break;
    case DataInputGetResult::DataAvailability::MoreDataMightBeAvailable:
      out << " (more data available)";
      break;
    case DataInputGetResult::DataAvailability::AllDataAvailable:;
    }
    return out;
  }
};

/**
 * Interface for types providing a way to extract a string from the DataType to perform matching
 * on.
 */
template <class DataType> class DataInput {
public:
  virtual ~DataInput() = default;

  virtual DataInputGetResult get(const DataType& data) const PURE;

  /**
   * Input type of DataInput.
   * String is default data input type since nearly all the DataInput's derived objects' input type
   * is string. The name of core types (e.g., std::string, int) is defined string constrant which
   * produces human-readable form (e.g., "string", "int").
   *
   * Override this function to provide matcher specific data input type.
   */
  virtual absl::string_view dataInputType() const { return DefaultMatchingDataType; }
};

template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;
template <class DataType> using DataInputFactoryCb = std::function<DataInputPtr<DataType>()>;

/**
 * Factory for data inputs.
 */
template <class DataType> class DataInputFactory : public Config::TypedFactory {
public:
  /**
   * Creates a DataInput from the provided config.
   */
  virtual DataInputFactoryCb<DataType>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  /**
   * The category of this factory depends on the DataType, so we require a name() function to exist
   * that allows us to get a string representation of the data type for categorization.
   */
  std::string category() const override {
    // Static assert to guide implementors to understand what is required.
    static_assert(std::is_convertible<absl::string_view, decltype(DataType::name())>(),
                  "DataType must implement valid name() function");
    return fmt::format("envoy.matching.{}.input", DataType::name());
  }
};

/**
 * Interface for types providing a way to use a string for matching without depending on protocol
 * data. As a result, these can be used for all protocols.
 */
class CommonProtocolInput {
public:
  virtual ~CommonProtocolInput() = default;
  virtual MatchingDataType get() PURE;
};
using CommonProtocolInputPtr = std::unique_ptr<CommonProtocolInput>;
using CommonProtocolInputFactoryCb = std::function<CommonProtocolInputPtr()>;

/**
 * Factory for CommonProtocolInput.
 */
class CommonProtocolInputFactory : public Config::TypedFactory {
public:
  /**
   * Creates a CommonProtocolInput from the provided config.
   */
  virtual CommonProtocolInputFactoryCb
  createCommonProtocolInputFactoryCb(const Protobuf::Message& config,
                                     ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.matching.common_inputs"; }
};

/**
 * Factory for registering custom matchers.
 */
template <class DataType> class CustomMatcherFactory : public Config::TypedFactory {
public:
  virtual MatchTreeFactoryCb<DataType>
  createCustomMatcherFactoryCb(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               DataInputFactoryCb<DataType> data_input,
                               absl::optional<OnMatchFactoryCb<DataType>> on_no_match,
                               OnMatchFactory<DataType>& on_match_factory) PURE;
  std::string category() const override {
    // Static assert to guide implementors to understand what is required.
    static_assert(std::is_convertible<absl::string_view, decltype(DataType::name())>(),
                  "DataType must implement valid name() function");
    return fmt::format("envoy.matching.{}.custom_matchers", DataType::name());
  }
};

} // namespace Matcher
} // namespace Envoy

// NOLINT(namespace-envoy)
namespace fmt {
// Allow fmtlib to use operator << defined in DataInputGetResult
template <> struct formatter<::Envoy::Matcher::DataInputGetResult> : ostream_formatter {};
} // namespace fmt
