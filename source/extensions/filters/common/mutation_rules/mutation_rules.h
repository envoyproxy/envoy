#pragma once

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/common/regex.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace MutationRules {

// The operation to check for. Note that the value of APPEND is to only be
// used when a second value will be added to a header that already exists.
enum class CheckOperation {
  // An attempt to replace all current header values
  SET,
  // An attempt to add a second value to a header that already exists.
  APPEND,
  // An attempt to remove a header
  REMOVE
};
enum class CheckResult { OK, IGNORE, FAIL };

class ExtraRoutingHeaders;

// Checker can be used to test a proposed change to an HTTP header against
// the mutation rules expressed in the *HeaderMutationRules* proto.
class Checker {
public:
  explicit Checker(const envoy::config::common::mutation_rules::v3::HeaderMutationRules& rules,
                   Regex::Engine& regex_engine);
  // Return whether the current rules allow the named header to be modified or removed.
  // The header name in question can include HTTP headers or internal headers
  // that start with ":". The result will specify whether the attempt should
  // be accepted, whether it should be silently ignored, or whether it should
  // cause the current HTTP operation to fail.
  CheckResult check(CheckOperation op, const Http::LowerCaseString& header_name,
                    absl::string_view header_value) const;

private:
  bool isAllowed(CheckOperation op, const Http::LowerCaseString& header_name) const;
  bool isValidValue(const Http::LowerCaseString& header_name, absl::string_view header_value) const;
  static const ExtraRoutingHeaders& extraRoutingHeaders();

  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules_;
  Regex::CompiledMatcherPtr allow_expression_;
  Regex::CompiledMatcherPtr disallow_expression_;
};

} // namespace MutationRules
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
