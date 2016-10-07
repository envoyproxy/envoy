#include "access_log_formatter.h"

#include "common/common/utility.h"

namespace Http {
namespace AccessLog {

const std::string FilterReasonUtils::NONE = "-";
const std::string FilterReasonUtils::FAILED_LOCAL_HEALTH_CHECK = "LH";
const std::string FilterReasonUtils::NO_HEALTHY_UPSTREAM = "UH";
const std::string FilterReasonUtils::UPSTREAM_REQUEST_TIMEOUT = "UT";
const std::string FilterReasonUtils::LOCAL_RESET = "LR";
const std::string FilterReasonUtils::UPSTREAM_REMOTE_RESET = "UR";
const std::string FilterReasonUtils::UPSTREAM_CONNECTION_FAILURE = "UF";
const std::string FilterReasonUtils::UPSTREAM_CONNECTION_TERMINATION = "UC";
const std::string FilterReasonUtils::UPSTREAM_OVERFLOW = "UO";
const std::string FilterReasonUtils::NO_ROUTE_FOUND = "NR";

const std::string& FilterReasonUtils::toShortString(FailureReason failure_reason) {
  switch (failure_reason) {
  case FailureReason::None:
    return NONE;
  case FailureReason::FailedLocalHealthCheck:
    return FAILED_LOCAL_HEALTH_CHECK;
  case FailureReason::NoHealthyUpstream:
    return NO_HEALTHY_UPSTREAM;
  case FailureReason::UpstreamRequestTimeout:
    return UPSTREAM_REQUEST_TIMEOUT;
  case FailureReason::LocalReset:
    return LOCAL_RESET;
  case FailureReason::UpstreamRemoteReset:
    return UPSTREAM_REMOTE_RESET;
  case FailureReason::UpstreamConnectionFailure:
    return UPSTREAM_CONNECTION_FAILURE;
  case FailureReason::UpstreamConnectionTermination:
    return UPSTREAM_CONNECTION_TERMINATION;
  case FailureReason::UpstreamOverflow:
    return UPSTREAM_OVERFLOW;
  case FailureReason::NoRouteFound:
    return NO_ROUTE_FOUND;
  }

  throw std::invalid_argument("Unknown failure_reason");
}

const std::string AccessLogFormatUtils::DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %FAILURE_REASON% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

FormatterPtr AccessLogFormatUtils::defaultAccessLogFormatter() {
  return FormatterPtr{new FormatterImpl(DEFAULT_FORMAT)};
}

FormatterImpl::FormatterImpl(const std::string& format) {
  formatters_ = AccessLogFormatParser::parse(format);
}

std::string FormatterImpl::format(const Http::HeaderMap& request_headers,
                                  const Http::HeaderMap& response_headers,
                                  const RequestInfo& request_info) const {
  std::string log_line;
  log_line.reserve(256);

  for (const FormatterPtr& formatter : formatters_) {
    log_line += formatter->format(request_headers, response_headers, request_info);
  }

  return log_line;
}

void AccessLogFormatParser::parseCommand(const std::string& token, const size_t start,
                                         std::string& main_header, std::string& alternative_header,
                                         Optional<size_t>& max_length) {
  size_t end_request = token.find(')', start);

  if (end_request != token.length() - 1) {
    // Closing bracket is not found.
    if (end_request == std::string::npos) {
      throw EnvoyException(fmt::format("Closing bracket is missing in token: {}", token));
    }

    // Closing bracket should be either last one or followed by ':' to denote limitation.
    if (token[end_request + 1] != ':') {
      throw EnvoyException(fmt::format("Incorrect position of ')' in token: {}", token));
    }

    std::string length_str = token.substr(end_request + 2);
    size_t length_value;

    if (!StringUtil::atoul(length_str.c_str(), length_value)) {
      throw EnvoyException(fmt::format("Length must be an integer, given: {}", length_str));
    }

    max_length.value(length_value);
  }

  std::string header_name = token.substr(start, end_request - start);
  size_t separator = header_name.find('?');

  if (separator == std::string::npos) {
    main_header = header_name;
    alternative_header = "";
  } else {
    main_header = header_name.substr(0, separator);
    alternative_header = header_name.substr(separator + 1, end_request - separator - 1);
  }
}

std::vector<FormatterPtr> AccessLogFormatParser::parse(const std::string& format) {
  std::string current_token;
  std::vector<FormatterPtr> formatters;

  for (size_t pos = 0; pos < format.length(); ++pos) {
    if (format[pos] == '%') {
      if (!current_token.empty()) {
        formatters.emplace_back(FormatterPtr{new PlainStringFormatter(current_token)});
        current_token = "";
      }

      size_t command_end_position = format.find('%', pos + 1);
      if (command_end_position == std::string::npos) {
        throw EnvoyException(fmt::format(
            "Incorrect configuration: {}. Expected end of operation '%', around position {}",
            format, pos));
      }
      std::string token = format.substr(pos + 1, command_end_position - (pos + 1));

      if (token.find("REQ(") == 0) {
        std::string main_header, alternative_header;
        Optional<size_t> max_length;
        const size_t start = 4;

        parseCommand(token, start, main_header, alternative_header, max_length);

        formatters.emplace_back(
            FormatterPtr(new RequestHeaderFormatter(main_header, alternative_header, max_length)));
      } else if (token.find("RESP(") == 0) {
        std::string main_header, alternative_header;
        Optional<size_t> max_length;
        const size_t start = 5;

        parseCommand(token, start, main_header, alternative_header, max_length);

        formatters.emplace_back(
            FormatterPtr(new ResponseHeaderFormatter(main_header, alternative_header, max_length)));
      } else {
        formatters.emplace_back(FormatterPtr(new RequestInfoFormatter(token)));
      }

      pos = command_end_position;
    } else {
      current_token += format[pos];
    }
  }

  if (!current_token.empty()) {
    formatters.emplace_back(FormatterPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

RequestInfoFormatter::RequestInfoFormatter(const std::string& field_name) {
  if (field_name == "START_TIME") {
    field_extractor_ = [](const RequestInfo& request_info) {
      return AccessLogDateTimeFormatter::fromTime(request_info.startTime());
    };
  } else if (field_name == "BYTES_RECEIVED") {
    field_extractor_ = [](const RequestInfo& request_info) {
      return std::to_string(request_info.bytesReceived());
    };
  } else if (field_name == "PROTOCOL") {
    field_extractor_ = [](const RequestInfo& request_info) { return request_info.protocol(); };
  } else if (field_name == "RESPONSE_CODE") {
    field_extractor_ = [](const RequestInfo& request_info) {
      return request_info.responseCode().valid()
                 ? std::to_string(request_info.responseCode().value())
                 : "0";
    };
  } else if (field_name == "BYTES_SENT") {
    field_extractor_ =
        [](const RequestInfo& request_info) { return std::to_string(request_info.bytesSent()); };
  } else if (field_name == "DURATION") {
    field_extractor_ = [](const RequestInfo& request_info) {
      return std::to_string(request_info.duration().count());
    };
  } else if (field_name == "FAILURE_REASON") {
    field_extractor_ = [](const RequestInfo& request_info) {
      return FilterReasonUtils::toShortString(request_info.failureReason());
    };
  } else if (field_name == "UPSTREAM_HOST") {
    field_extractor_ = [](const RequestInfo& request_info) {
      std::string upstream_host_url;
      if (nullptr != request_info.upstreamHost()) {
        upstream_host_url = request_info.upstreamHost()->url();
      }

      return upstream_host_url.empty() ? "-" : upstream_host_url;
    };
  } else {
    throw EnvoyException(fmt::format("Not supported field in RequestInfo: {}", field_name));
  }
}

std::string RequestInfoFormatter::format(const HeaderMap&, const HeaderMap&,
                                         const RequestInfo& request_info) const {
  return field_extractor_(request_info);
}

PlainStringFormatter::PlainStringFormatter(const std::string& str) : str_(str) {}

std::string PlainStringFormatter::format(const Http::HeaderMap&, const Http::HeaderMap&,
                                         const RequestInfo&) const {
  return str_;
}

HeaderFormatter::HeaderFormatter(const std::string& main_header,
                                 const std::string& alternative_header,
                                 const Optional<size_t>& max_length)
    : main_header_(main_header), alternative_header_(alternative_header), max_length_(max_length) {}

std::string HeaderFormatter::format(const HeaderMap& headers) const {
  std::string header_value = headers.get(main_header_);

  if (header_value.empty() && !alternative_header_.get().empty()) {
    header_value = headers.get(alternative_header_);
  }

  if (header_value.empty()) {
    header_value = "-";
  }

  if (max_length_.valid() && header_value.length() > max_length_.value()) {
    return header_value.substr(0, max_length_.value());
  }

  return header_value;
}

ResponseHeaderFormatter::ResponseHeaderFormatter(const std::string& main_header,
                                                 const std::string& alternative_header,
                                                 const Optional<size_t>& max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string ResponseHeaderFormatter::format(const Http::HeaderMap&,
                                            const Http::HeaderMap& response_headers,
                                            const RequestInfo&) const {
  return HeaderFormatter::format(response_headers);
}

RequestHeaderFormatter::RequestHeaderFormatter(const std::string& main_header,
                                               const std::string& alternative_header,
                                               const Optional<size_t>& max_length)
    : HeaderFormatter(main_header, alternative_header, max_length) {}

std::string RequestHeaderFormatter::format(const Http::HeaderMap& request_headers,
                                           const Http::HeaderMap&, const RequestInfo&) const {
  return HeaderFormatter::format(request_headers);
}

} // AccessLog
} // Http
