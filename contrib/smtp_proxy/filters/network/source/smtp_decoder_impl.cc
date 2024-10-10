#include "contrib/smtp_proxy/filters/network/source/smtp_decoder_impl.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

DecoderImpl::DecoderImpl(DecoderCallbacks* callbacks, TimeSource& time_source,
                         Random::RandomGenerator& random_generator)
    : callbacks_(callbacks), time_source_(time_source), random_generator_(random_generator) {
  session_ = new SmtpSession(callbacks, time_source_, random_generator_);
}

SmtpUtils::Result DecoderImpl::onData(Buffer::Instance& data, bool upstream) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  if (upstream) {
    result = parseResponse(data);
    data.drain(data.length());
    return result;
  }
  result = parseCommand(data);
  data.drain(data.length());
  return result;
}

SmtpUtils::Result DecoderImpl::parseCommand(Buffer::Instance& data) {
  ENVOY_LOG(debug, "smtp_proxy parseCommand: decoding {} bytes", data.length());

  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (getSession()->isTerminated()) {
    return result;
  }
  if (getSession()->isDataTransferInProgress()) {
    getSession()->updateBytesMeterOnCommand(data);
    return result;
  }

  std::string buffer = data.toString();
  ENVOY_LOG(debug, "received command {}", buffer);
  // Each SMTP command ends with CRLF ("\r\n"), if received buffer doesn't end with CRLF, the filter
  // will not process it.
  if (!absl::EndsWith(buffer, SmtpUtils::smtpCrlfSuffix)) {
    return result;
  }

  buffer = StringUtil::cropRight(buffer, SmtpUtils::smtpCrlfSuffix);
  int length = buffer.length();

  std::string command = "";
  std::string args = "";
  if (length < 4) {
    return result;
  } else if (length == 4) {
    command = buffer;
  } else if (length > 4) {
    // 4 letter command with some args after a space. i.e. cmd should have at least length=6
    if (length >= 6 && buffer[4] == ' ' && buffer[5] != ' ') {
      command = buffer.substr(0, 4);
      args = buffer.substr(5);
    } else if (absl::EqualsIgnoreCase(buffer, SmtpUtils::startTlsCommand)) {
      command = SmtpUtils::startTlsCommand;
    }
  }
  result = session_->handleCommand(command, args);
  return result;
}

SmtpUtils::Result DecoderImpl::parseResponse(Buffer::Instance& data) {
  ENVOY_LOG(debug, "smtp_proxy: decoding response {} bytes", data.length());

  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  // Special handling to parse any error response to connection request.
  if (!(session_->isCommandInProgress()) &&
      session_->getState() != SmtpSession::State::ConnectionRequest) {
    return result;
  }

  std::string response = data.toString();
  if (!absl::EndsWith(response, SmtpUtils::smtpCrlfSuffix)) {
    return result;
  }

  response = StringUtil::cropRight(response, SmtpUtils::smtpCrlfSuffix);
  int length = response.length();
  if (length < 3) {
    // Minimum 3 byte response code needed to parse response from server.
    return result;
  }
  std::string response_code_str = response.substr(0, 3);
  uint16_t response_code = 0;
  try {
    response_code = stoi(response_code_str);
  } catch (...) {
    response_code = 0;
    ENVOY_LOG(error, "smtp_proxy: error while decoding response code ", response_code);
  }
  result = session_->handleResponse(response_code, response);

  return result;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy