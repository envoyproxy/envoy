#include "source/common/formatter/substitution_format_utility.h"

#include "envoy/api/os_sys_calls.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

absl::Status CommandSyntaxChecker::verifySyntax(CommandSyntaxChecker::CommandSyntaxFlags flags,
                                                absl::string_view command,
                                                absl::string_view subcommand,
                                                absl::optional<size_t> length) {
  if ((flags == COMMAND_ONLY) && ((subcommand.length() != 0) || length.has_value())) {
    return absl::InvalidArgumentError(
        fmt::format("{} does not take any parameters or length", command));
  }

  if ((flags & PARAMS_REQUIRED).any() && (subcommand.length() == 0)) {
    return absl::InvalidArgumentError(fmt::format("{} requires parameters", command));
  }

  if ((flags & LENGTH_ALLOWED).none() && length.has_value()) {
    return absl::InvalidArgumentError(
        fmt::format("{} does not allow length to be specified.", command));
  }
  return absl::OkStatus();
}

const absl::optional<std::reference_wrapper<const std::string>>
SubstitutionFormatUtils::protocolToString(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return absl::nullopt;
}

const std::string&
SubstitutionFormatUtils::protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol) {
  if (protocol) {
    return Http::Utility::getProtocolString(protocol.value());
  }
  return DefaultUnspecifiedValueString;
}

const absl::optional<std::string> SubstitutionFormatUtils::getHostname() {
#ifdef HOST_NAME_MAX
  const size_t len = HOST_NAME_MAX;
#else
  // This is notably the case in OSX.
  const size_t len = 255;
#endif
  char name[len];
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallIntResult result = os_sys_calls.gethostname(name, len);

  absl::optional<std::string> hostname;
  if (result.return_value_ == 0) {
    hostname = name;
  }

  return hostname;
}

const ProtobufWkt::Value& SubstitutionFormatUtils::unspecifiedValue() {
  return ValueUtil::nullValue();
}

void SubstitutionFormatUtils::truncate(std::string& str, absl::optional<size_t> max_length) {
  if (!max_length) {
    return;
  }

  if (str.length() > max_length.value()) {
    str.resize(max_length.value());
  }
}

absl::string_view SubstitutionFormatUtils::truncateStringView(absl::string_view str,
                                                              absl::optional<size_t> max_length) {
  if (!max_length) {
    return str;
  }

  return str.substr(0, max_length.value());
}

absl::StatusOr<SubstitutionFormatUtils::HeaderPair>
SubstitutionFormatUtils::parseSubcommandHeaders(absl::string_view subcommand) {
  absl::string_view main_header, alternative_header;
  // subs is used only to check if there are more than 2 headers separated by '?'.
  std::vector<absl::string_view> subs;
  parseSubcommand(subcommand, '?', main_header, alternative_header, subs);
  if (!subs.empty()) {
    return absl::InvalidArgumentError(
        // Header format rules support only one alternative header.
        // docs/root/configuration/observability/access_log/access_log.rst#format-rules
        absl::StrCat("More than 1 alternative header specified in token: ", subcommand));
  }

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.consistent_header_validation")) {
    if (!Http::HeaderUtility::headerNameIsValid(absl::AsciiStrToLower(main_header)) ||
        !Http::HeaderUtility::headerNameIsValid(absl::AsciiStrToLower(alternative_header))) {
      return absl::InvalidArgumentError(
          "Invalid header configuration. Format string contains invalid characters.");
    }
  } else {
    // The main and alternative header should not contain invalid characters {NUL, LR, CF}.
    if (!Envoy::Http::validHeaderString(main_header) ||
        !Envoy::Http::validHeaderString(alternative_header)) {
      return absl::InvalidArgumentError(
          "Invalid header configuration. Format string contains null or newline.");
    }
  }
  return {std::make_pair(main_header, alternative_header)};
}

} // namespace Formatter
} // namespace Envoy
