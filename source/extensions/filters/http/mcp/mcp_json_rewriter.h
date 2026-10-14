#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

class TraceparentRewriter : public nlohmann::json::json_sax_t {
public:
  explicit TraceparentRewriter(absl::string_view new_parent_id) : new_parent_id_(new_parent_id) {}

  bool null() override;
  bool boolean(bool val) override;
  bool number_integer(number_integer_t val) override;
  bool number_unsigned(number_unsigned_t val) override;
  bool number_float(number_float_t, const string_t& s) override;
  bool string(string_t& val) override;
  bool binary(binary_t&) override;
  bool start_object(std::size_t) override;
  bool key(string_t& val) override;
  bool end_object() override;
  bool start_array(std::size_t) override;
  bool end_array() override;
  bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception& ex) override;

  absl::StatusOr<std::string> finish(bool parse_ok) &&;

private:
  struct Scope {
    bool is_object;
    bool first_element = true;
    std::string current_key;
  };

  bool isAtParams() const;
  bool isAtParamsMeta() const;
  bool isAtParamsMetaTraceparent() const;
  bool beginValue(bool is_object, bool is_string);

  std::vector<Scope> stack_;
  std::string output_;
  absl::string_view new_parent_id_;
  absl::Status status_ = absl::OkStatus();
  bool found_params_ = false;
  bool found_meta_ = false;
  bool found_traceparent_ = false;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
