#include "source/extensions/filters/http/mcp/mcp_json_rewriter.h"

#include <cstddef>
#include <string>
#include <utility>

#include "source/extensions/filters/common/mcp/constants.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

using SpanContext = Envoy::Extensions::Tracers::OpenTelemetry::SpanContext;
using SpanContextExtractor = Envoy::Extensions::Tracers::OpenTelemetry::SpanContextExtractor;

bool TraceparentRewriter::null() {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  output_ += "null";
  return true;
}

bool TraceparentRewriter::boolean(bool val) {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  output_ += val ? "true" : "false";
  return true;
}

bool TraceparentRewriter::number_integer(number_integer_t val) {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  absl::StrAppend(&output_, val);
  return true;
}

bool TraceparentRewriter::number_unsigned(number_unsigned_t val) {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  absl::StrAppend(&output_, val);
  return true;
}

bool TraceparentRewriter::number_float(number_float_t, const string_t& s) {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  output_ += s;
  return true;
}

bool TraceparentRewriter::string(string_t& val) {
  const bool is_traceparent = isAtParamsMetaTraceparent();
  if (!beginValue(/*is_object=*/false, /*is_string=*/true)) {
    return false;
  }
  if (is_traceparent) {
    absl::StatusOr<SpanContext> parsed_traceparent =
        SpanContextExtractor::parseTraceparent(val, "");
    if (!parsed_traceparent.ok()) {
      status_ = parsed_traceparent.status();
      return false;
    }
    parsed_traceparent->setSpanId(std::string(new_parent_id_));
    const std::string new_traceparent =
        SpanContextExtractor::serializeTraceparent(*parsed_traceparent);
    output_ += nlohmann::json(new_traceparent).dump();
    return true;
  }
  output_ += nlohmann::json(val).dump();
  return true;
}

bool TraceparentRewriter::binary(binary_t&) { return false; }

bool TraceparentRewriter::start_object(std::size_t) {
  if (!beginValue(/*is_object=*/true, /*is_string=*/false)) {
    return false;
  }
  output_.push_back('{');
  stack_.push_back(Scope{/*is_object=*/true});
  return true;
}

bool TraceparentRewriter::key(string_t& val) {
  Scope& scope = stack_.back();
  if (!scope.first_element) {
    output_.push_back(',');
  }
  scope.first_element = false;
  scope.current_key = val;
  output_ += nlohmann::json(val).dump();
  output_.push_back(':');
  return true;
}

bool TraceparentRewriter::end_object() {
  stack_.pop_back();
  output_.push_back('}');
  return true;
}

bool TraceparentRewriter::start_array(std::size_t) {
  if (!beginValue(/*is_object=*/false, /*is_string=*/false)) {
    return false;
  }
  output_.push_back('[');
  stack_.push_back(Scope{/*is_object=*/false});
  return true;
}

bool TraceparentRewriter::end_array() {
  stack_.pop_back();
  output_.push_back(']');
  return true;
}

bool TraceparentRewriter::parse_error(std::size_t, const std::string&,
                                      const nlohmann::json::exception& ex) {
  status_ = absl::InvalidArgumentError(ex.what());
  return false;
}

absl::StatusOr<std::string> TraceparentRewriter::finish(bool parse_ok) && {
  if (!status_.ok()) {
    return status_;
  }
  if (!parse_ok) {
    return absl::InvalidArgumentError("Failed to parse body to JSON");
  }
  if (!found_params_) {
    return absl::InvalidArgumentError("Failed to find params field");
  }
  if (!found_meta_) {
    return absl::InvalidArgumentError("Failed to find _meta in params");
  }
  if (!found_traceparent_) {
    return absl::InvalidArgumentError("Failed to find traceparent in _meta");
  }
  return std::move(output_);
}

bool TraceparentRewriter::isAtParams() const {
  return stack_.size() == 1 && stack_[0].is_object &&
         stack_[0].current_key == Filters::Common::Mcp::McpConstants::PARAMS_FIELD;
}

bool TraceparentRewriter::isAtParamsMeta() const {
  return stack_.size() == 2 && stack_[0].is_object &&
         stack_[0].current_key == Filters::Common::Mcp::McpConstants::PARAMS_FIELD &&
         stack_[1].is_object &&
         stack_[1].current_key == Filters::Common::Mcp::McpConstants::META_FIELD;
}

bool TraceparentRewriter::isAtParamsMetaTraceparent() const {
  return stack_.size() == 3 && stack_[0].is_object &&
         stack_[0].current_key == Filters::Common::Mcp::McpConstants::PARAMS_FIELD &&
         stack_[1].is_object &&
         stack_[1].current_key == Filters::Common::Mcp::McpConstants::META_FIELD &&
         stack_[2].is_object &&
         stack_[2].current_key == Filters::Common::Mcp::McpConstants::TRACEPARENT_FIELD;
}

bool TraceparentRewriter::beginValue(bool is_object, bool is_string) {
  if (isAtParams()) {
    if (!is_object) {
      status_ = absl::InvalidArgumentError("Failed to parse params field to JSON");
      return false;
    }
    found_params_ = true;
  } else if (isAtParamsMeta()) {
    if (!is_object) {
      status_ = absl::InvalidArgumentError("Failed to parse _meta field to JSON");
      return false;
    }
    found_meta_ = true;
  } else if (isAtParamsMetaTraceparent()) {
    if (!is_string) {
      status_ = absl::InvalidArgumentError("Failed to parse traceparent field to string");
      return false;
    }
    found_traceparent_ = true;
  }

  if (!stack_.empty()) {
    Scope& scope = stack_.back();
    if (!scope.is_object) {
      if (!scope.first_element) {
        output_.push_back(',');
      }
      scope.first_element = false;
    }
  }
  return true;
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
