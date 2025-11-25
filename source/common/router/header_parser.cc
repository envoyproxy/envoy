#include "source/common/router/header_parser.h"

#include <cctype>
#include <memory>
#include <regex>
#include <string>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Router {

namespace {

absl::StatusOr<Formatter::FormatterPtr>
parseHttpHeaderFormatter(const envoy::config::core::v3::HeaderValue& header_value,
                         const Formatter::CommandParserPtrVector& command_parsers) {
  const std::string& key = header_value.key();
  // PGV constraints provide this guarantee.
  ASSERT(!key.empty());
  // We reject :path/:authority rewriting, there is already a well defined mechanism to
  // perform this in the RouteAction, and doing this via request_headers_to_add
  // will cause us to have to worry about interaction with other aspects of the
  // RouteAction, e.g. prefix rewriting. We also reject other :-prefixed
  // headers, since it seems dangerous and there doesn't appear a use case.
  // Host is disallowed as it created confusing and inconsistent behaviors for
  // HTTP/1 and HTTP/2. It could arguably be allowed on the response path.
  if (!Http::HeaderUtility::isModifiableHeader(key)) {
    return absl::InvalidArgumentError(":-prefixed or host headers may not be modified");
  }

  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.remove_legacy_route_formatter")) {
    // UPSTREAM_METADATA and DYNAMIC_METADATA must be translated from JSON ["a", "b"] format to
    // colon format (a:b)
    std::string final_header_value = HeaderParser::translateMetadataFormat(header_value.value());
    // Change PER_REQUEST_STATE to FILTER_STATE.
    final_header_value = HeaderParser::translatePerRequestState(final_header_value);
    // Let the substitution formatter parse the final_header_value.
    return Envoy::Formatter::FormatterImpl::create(final_header_value, true, command_parsers);
  }

  // Let the substitution formatter parse the header_value.
  return Envoy::Formatter::FormatterImpl::create(header_value.value(), true, command_parsers);
}

} // namespace

HeadersToAddEntry::HeadersToAddEntry(const HeaderValueOption& header_value_option,
                                     const Formatter::CommandParserPtrVector& command_parsers,
                                     absl::Status& creation_status)
    : original_value_(header_value_option.header().value()),
      add_if_empty_(header_value_option.keep_empty_value()) {

  if (header_value_option.has_append()) {
    // 'append' is set and ensure the 'append_action' value is equal to the default value.
    if (header_value_option.append_action() != HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
      creation_status =
          absl::InvalidArgumentError("Both append and append_action are set and it's not allowed");
      return;
    }

    append_action_ = header_value_option.append().value()
                         ? HeaderValueOption::APPEND_IF_EXISTS_OR_ADD
                         : HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD;
  } else {
    append_action_ = header_value_option.append_action();
  }

  auto formatter_or_error = parseHttpHeaderFormatter(header_value_option.header(), command_parsers);
  SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
  formatter_ = std::move(formatter_or_error.value());
}

HeadersToAddEntry::HeadersToAddEntry(const HeaderValue& header_value,
                                     HeaderAppendAction append_action,
                                     const Formatter::CommandParserPtrVector& command_parsers,
                                     absl::Status& creation_status)
    : original_value_(header_value.value()), append_action_(append_action) {
  auto formatter_or_error = parseHttpHeaderFormatter(header_value, command_parsers);
  SET_AND_RETURN_IF_NOT_OK(formatter_or_error.status(), creation_status);
  formatter_ = std::move(formatter_or_error.value());
}

absl::StatusOr<HeaderParserPtr>
HeaderParser::configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());
  header_parser->headers_to_add_.reserve(headers_to_add.size());
  for (const auto& header_value_option : headers_to_add) {
    auto entry_or_error = HeadersToAddEntry::create(header_value_option);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()),
        std::move(entry_or_error.value()));
  }

  return header_parser;
}

absl::StatusOr<HeaderParserPtr> HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers_to_add,
    HeaderAppendAction append_action) {
  HeaderParserPtr header_parser(new HeaderParser());

  header_parser->headers_to_add_.reserve(headers_to_add.size());
  for (const auto& header_value : headers_to_add) {
    auto entry_or_error = HeadersToAddEntry::create(header_value, append_action);
    RETURN_IF_NOT_OK_REF(entry_or_error.status());
    header_parser->headers_to_add_.emplace_back(Http::LowerCaseString(header_value.key()),
                                                std::move(entry_or_error.value()));
  }

  return header_parser;
}

absl::StatusOr<HeaderParserPtr>
HeaderParser::configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add,
                        const Protobuf::RepeatedPtrField<std::string>& headers_to_remove) {
  auto parser_or_error = configure(headers_to_add);
  RETURN_IF_NOT_OK_REF(parser_or_error.status());
  HeaderParserPtr header_parser = std::move(parser_or_error.value());

  header_parser->headers_to_remove_.reserve(headers_to_remove.size());
  for (const auto& header : headers_to_remove) {
    // We reject :-prefix (e.g. :path) removal here. This is dangerous, since other aspects of
    // request finalization assume their existence and they are needed for well-formedness in most
    // cases.
    if (!Http::HeaderUtility::isRemovableHeader(header)) {
      return absl::InvalidArgumentError(":-prefixed or host headers may not be removed");
    }
    header_parser->headers_to_remove_.emplace_back(header);
  }

  return header_parser;
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context& context,
                                   const StreamInfo::StreamInfo& stream_info) const {
  evaluateHeaders(headers, context, &stream_info);
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context& context,
                                   const StreamInfo::StreamInfo* stream_info) const {
  // Removing headers in the headers_to_remove_ list first makes
  // remove-before-add the default behavior as expected by users.
  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }

  // Temporary storage to hold evaluated values of headers to add and replace. This is required
  // to execute all formatters using the original received headers.
  // Only after all the formatters produced the new values of the headers, the headers are set.
  // absl::InlinedVector is optimized for 4 headers. After that it behaves as normal std::vector.
  // It is assumed that most of the use cases will add or modify fairly small number of headers
  // (<=4). If this assumption changes, the number of inlined capacity should be increased.
  // header_formatter_speed_test.cc provides micro-benchmark for evaluating speed of adding and
  // replacing headers and should be used when modifying the code below to access the performance
  // impact of code changes.
  absl::InlinedVector<std::pair<const Http::LowerCaseString&, const std::string>, 4> headers_to_add,
      headers_to_overwrite;
  // value_buffer is used only when stream_info is a valid pointer and stores header value
  // created by a formatter. It is declared outside of 'for' loop for performance reason to avoid
  // stack allocation and unnecessary std::string's memory adjustments for each iteration. The
  // actual value of the header is accessed via 'value' variable which is initialized differently
  // depending whether stream_info and valid or nullptr. Based on performance tests implemented in
  // header_formatter_speed_test.cc this approach strikes the best balance between performance and
  // readability.
  std::string value_buffer;
  for (const auto& [key, entry] : headers_to_add_) {
    absl::string_view value;
    if (stream_info != nullptr) {
      value_buffer = entry->formatter_->format(context, *stream_info);
      value = value_buffer;
    } else {
      value = entry->original_value_;
    }
    if (!value.empty() || entry->add_if_empty_) {
      switch (entry->append_action_) {
        PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        headers_to_add.emplace_back(key, value);
        break;
      case HeaderValueOption::ADD_IF_ABSENT:
        if (auto header_entry = headers.get(key); header_entry.empty()) {
          headers_to_add.emplace_back(key, value);
        }
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS:
        if (headers.get(key).empty()) {
          break;
        }
        FALLTHRU;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        headers_to_overwrite.emplace_back(key, value);
        break;
      }
    }
  }

  // First overwrite all headers which need to be overwritten.
  for (const auto& header : headers_to_overwrite) {
    headers.setReferenceKey(header.first, header.second);
  }

  // Now add headers which should be added.
  for (const auto& header : headers_to_add) {
    headers.addReferenceKey(header.first, header.second);
  }
}

Http::HeaderTransforms HeaderParser::getHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                         bool do_formatting) const {
  Http::HeaderTransforms transforms;

  for (const auto& [key, entry] : headers_to_add_) {
    if (do_formatting) {
      const std::string value = entry->formatter_->format({}, stream_info);
      if (!value.empty() || entry->add_if_empty_) {
        switch (entry->append_action_) {
        case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
          transforms.headers_to_append_or_add.push_back({key, value});
          break;
        case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
          transforms.headers_to_overwrite_or_add.push_back({key, value});
          break;
        case HeaderValueOption::ADD_IF_ABSENT:
          transforms.headers_to_add_if_absent.push_back({key, value});
          break;
        default:
          break;
        }
      }
    } else {
      switch (entry->append_action_) {
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        transforms.headers_to_append_or_add.push_back({key, entry->original_value_});
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        transforms.headers_to_overwrite_or_add.push_back({key, entry->original_value_});
        break;
      case HeaderValueOption::ADD_IF_ABSENT:
        transforms.headers_to_add_if_absent.push_back({key, entry->original_value_});
        break;
      default:
        break;
      }
    }
  }

  transforms.headers_to_remove = headers_to_remove_;

  return transforms;
}

} // namespace Router
} // namespace Envoy
