#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Filters::Common::MutationRules::Checker;
using Filters::Common::MutationRules::CheckOperation;
using Filters::Common::MutationRules::CheckResult;
using Http::Headers;
using Http::LowerCaseString;
using Stats::Counter;

using envoy::service::ext_proc::v3::BodyMutation;
using envoy::service::ext_proc::v3::HeaderMutation;

bool MutationUtils::headerInAllowList(
    absl::string_view key, const std::vector<Matchers::StringMatcherPtr>& header_matchers) {
  return std::any_of(header_matchers.begin(), header_matchers.end(),
                     [&key](auto& matcher) { return matcher->match(key); });
}

void MutationUtils::headersToProto(const Http::HeaderMap& headers_in,
                                   const std::vector<Matchers::StringMatcherPtr>& header_matchers,
                                   envoy::config::core::v3::HeaderMap& proto_out) {
  headers_in.iterate([&proto_out,
                      &header_matchers](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    if (header_matchers.empty() || headerInAllowList(e.key().getStringView(), header_matchers)) {
      auto* new_header = proto_out.add_headers();
      new_header->set_key(std::string(e.key().getStringView()));
      new_header->set_value(MessageUtil::sanitizeUtf8String(e.value().getStringView()));
    }
    return Http::HeaderMap::Iterate::Continue;
  });
}

absl::Status MutationUtils::responseHeaderSizeCheck(const uint32_t max_request_headers_kb,
                                                    const uint32_t max_request_headers_count,
                                                    const HeaderMutation& mutation,
                                                    Counter& rejected_mutations) {
  const auto& remove_headers = mutation.remove_headers();
  if (remove_headers.size() > static_cast<int>(max_request_headers_count) ||
      remove_headers.SpaceUsedExcludingSelf() > static_cast<int>(max_request_headers_kb * 1024)) {
    ENVOY_LOG(debug,
              "Mutation remove header count {} or mutation remove header size {} may exceed the "
              "limit. Returning error.",
              remove_headers.size(), remove_headers.SpaceUsedExcludingSelf());
    rejected_mutations.inc();
    return absl::InvalidArgumentError(absl::StrCat(
        "Remove header count ", std::to_string(remove_headers.size()), " or remove header size ",
        std::to_string(remove_headers.SpaceUsedExcludingSelf()),
        " exceed the HCM header size limit."));
  }

  const auto& set_headers = mutation.set_headers();
  if (set_headers.size() > static_cast<int>(max_request_headers_count) ||
      set_headers.SpaceUsedExcludingSelf() > static_cast<int>(max_request_headers_kb * 1024)) {
    ENVOY_LOG(debug,
              "Mutation set header count {} or mutation set header size {} may exceed the limit. "
              "Returning error.",
              set_headers.size(), set_headers.SpaceUsedExcludingSelf());
    rejected_mutations.inc();
    return absl::InvalidArgumentError(
        absl::StrCat("Set header count ", std::to_string(set_headers.size()),
                     " or set header size ", std::to_string(set_headers.SpaceUsedExcludingSelf()),
                     " exceed the HCM header size limit."));
  }
  return absl::OkStatus();
}

absl::Status MutationUtils::headerMutationResultCheck(const uint32_t max_request_headers_kb,
                                                      const uint32_t max_request_headers_count,
                                                      const Http::HeaderMap& headers,
                                                      Counter& rejected_mutations) {
  if (headers.byteSize() > max_request_headers_kb * 1024 ||
      headers.size() > max_request_headers_count) {
    ENVOY_LOG(debug,
              "After mutation, the total header count {} or total header size {} exceed the "
              "limit. Returning error.",
              headers.size(), headers.byteSize());
    rejected_mutations.inc();
    return absl::InvalidArgumentError(
        absl::StrCat("Header mutation causes end result header count ", headers.size(),
                     " or header size ", headers.byteSize(), " exceeding the limit."));
  }
  return absl::OkStatus();
}

absl::Status MutationUtils::applyHeaderMutations(const uint32_t max_request_headers_kb,
                                                 const uint32_t max_request_headers_count,
                                                 const HeaderMutation& mutation,
                                                 Http::HeaderMap& headers, bool replacing_message,
                                                 const Checker& checker,
                                                 Counter& rejected_mutations) {
  // Check whether the remove_headers or set_headers size exceed the HTTP connection manager limit.
  // Reject the mutation and return error status if either one does.
  const auto result = responseHeaderSizeCheck(max_request_headers_kb, max_request_headers_count,
                                              mutation, rejected_mutations);
  if (!result.ok()) {
    return result;
  }

  for (const auto& hdr : mutation.remove_headers()) {
    if (!Http::HeaderUtility::headerNameIsValid(hdr)) {
      ENVOY_LOG(debug, "remove_headers contain invalid character, may not be removed.");
      rejected_mutations.inc();
      return absl::InvalidArgumentError("Invalid character in remove_headers mutation.");
    }
    const LowerCaseString remove_header(hdr);
    switch (checker.check(CheckOperation::REMOVE, remove_header, "")) {
    case CheckResult::OK:
      ENVOY_LOG(trace, "Removing header {}", remove_header);
      headers.remove(remove_header);
      break;
    case CheckResult::IGNORE:
      ENVOY_LOG(debug, "Header {} may not be removed per rules", remove_header);
      rejected_mutations.inc();
      break;
    case CheckResult::FAIL:
      ENVOY_LOG(debug, "Header {} may not be removed. Returning error", remove_header);
      rejected_mutations.inc();
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid attempt to remove ", remove_header.get()));
    }
  }

  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }
    if (!Http::HeaderUtility::headerNameIsValid(sh.header().key()) ||
        !Http::HeaderUtility::headerValueIsValid(sh.header().value())) {
      ENVOY_LOG(debug,
                "set_headers contain invalid character in key or value, may not be appended.");
      rejected_mutations.inc();
      return absl::InvalidArgumentError("Invalid character in set_headers mutation.");
    }
    const LowerCaseString header_name(sh.header().key());
    const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(sh, append, false);
    const auto check_op = (append && !headers.get(header_name).empty()) ? CheckOperation::APPEND
                                                                        : CheckOperation::SET;
    auto check_result = checker.check(check_op, header_name, sh.header().value());
    if (replacing_message && header_name == Http::Headers::get().Method) {
      // Special handling to allow changing ":method" when the
      // CONTINUE_AND_REPLACE option is selected, to stay compatible.
      check_result = CheckResult::OK;
    }
    switch (check_result) {
    case CheckResult::OK:
      ENVOY_LOG(trace, "Setting header {} append = {}", sh.header().key(), append);
      if (append) {
        headers.addCopy(header_name, sh.header().value());
      } else {
        headers.setCopy(header_name, sh.header().value());
      }
      break;
    case CheckResult::IGNORE:
      ENVOY_LOG(debug, "Header {} may not be modified per rules", header_name);
      rejected_mutations.inc();
      break;
    case CheckResult::FAIL:
      ENVOY_LOG(debug, "Header {} may not be modified. Returning error", header_name);
      rejected_mutations.inc();
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid attempt to modify ", static_cast<absl::string_view>(header_name)));
    }
  }

  // After header mutation, check the ending headers are not exceeding the HCM limit.
  return headerMutationResultCheck(max_request_headers_kb, max_request_headers_count, headers,
                                   rejected_mutations);
}

void MutationUtils::applyBodyMutations(const BodyMutation& mutation, Buffer::Instance& buffer) {
  switch (mutation.mutation_case()) {
  case BodyMutation::MutationCase::kClearBody:
    if (mutation.clear_body()) {
      ENVOY_LOG(trace, "Clearing HTTP body");
      buffer.drain(buffer.length());
    }
    break;
  case BodyMutation::MutationCase::kBody:
    ENVOY_LOG(trace, "Replacing body of {} bytes with new body of {} bytes", buffer.length(),
              mutation.body().size());
    buffer.drain(buffer.length());
    buffer.add(mutation.body());
    break;
  default:
    // Nothing to do on default
    break;
  }
}

bool MutationUtils::isValidHttpStatus(int code) { return (code >= 200); }

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
