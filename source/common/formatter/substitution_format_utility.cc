#include "source/common/formatter/substitution_format_utility.h"

#include "envoy/api/os_sys_calls.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Formatter {

static const std::string DefaultUnspecifiedValueString = "-";

void CommandSyntaxChecker::verifySyntax(CommandSyntaxFlags flags, const std::string& command,
                                        const std::string& subcommand,
                                        const absl::optional<size_t>& length) {
  if ((flags == COMMAND_ONLY) && ((subcommand.length() != 0) || length.has_value())) {
    throwEnvoyExceptionOrPanic(fmt::format("{} does not take any parameters or length", command));
  }

  if ((flags & PARAMS_REQUIRED).any() && (subcommand.length() == 0)) {
    throwEnvoyExceptionOrPanic(fmt::format("{} requires parameters", command));
  }

  if ((flags & LENGTH_ALLOWED).none() && length.has_value()) {
    throwEnvoyExceptionOrPanic(fmt::format("{} does not allow length to be specified.", command));
  }
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

const std::string SubstitutionFormatUtils::getHostnameOrDefault() {
  absl::optional<std::string> hostname = getHostname();
  if (hostname.has_value()) {
    return hostname.value();
  }
  return DefaultUnspecifiedValueString;
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

void SubstitutionFormatUtils::parseSubcommandHeaders(const std::string& subcommand,
                                                     std::string& main_header,
                                                     std::string& alternative_header) {
  // subs is used only to check if there are more than 2 headers separated by '?'.
  std::vector<std::string> subs;
  alternative_header = "";
  parseSubcommand(subcommand, '?', main_header, alternative_header, subs);
  if (!subs.empty()) {
    throwEnvoyExceptionOrPanic(
        // Header format rules support only one alternative header.
        // docs/root/configuration/observability/access_log/access_log.rst#format-rules
        absl::StrCat("More than 1 alternative header specified in token: ", subcommand));
  }

  // The main and alternative header should not contain invalid characters {NUL, LR, CF}.
  if (!Envoy::Http::validHeaderString(main_header) ||
      !Envoy::Http::validHeaderString(alternative_header)) {
    throwEnvoyExceptionOrPanic(
        "Invalid header configuration. Format string contains null or newline.");
  }
}

} // namespace Formatter
} // namespace Envoy
