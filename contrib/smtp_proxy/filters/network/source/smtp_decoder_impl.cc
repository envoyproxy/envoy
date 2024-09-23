#include "contrib/smtp_proxy/filters/network/source/smtp_decoder_impl.h"

#include "source/common/common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpUtils::Result DecoderImpl::parseCommand(Buffer::Instance& data, Command& command) {
  ENVOY_LOG(debug, "smtp_proxy parseCommand: decoding {} bytes", data.length());
  ENVOY_LOG(debug, "smtp_proxy received command: {}", data.toString());

  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  // std::string buffer = data.toString();
  // ENVOY_LOG(debug, "received command {}", buffer);
  // // Each SMTP command ends with CRLF ("\r\n"), if received data doesn't end with CRLF, the
  // decoder
  // // will buffer the data until it receives CRLF .
  // // command should end with /r/n
  // //  There shouldn't be multiple /r/n
  // //  Extract the command verb and args, only if verb is known, process it. else pass it to
  // upstream. if (!absl::EndsWith(buffer, SmtpUtils::smtpCrlfSuffix)) {
  //   result = SmtpUtils::Result::NeedMoreData;
  //   return result;
  // }

  // buffer = StringUtil::cropRight(buffer, SmtpUtils::smtpCrlfSuffix);

  // const bool trailing_crlf_consumed = absl::ConsumeSuffix(&buffer, CRLF);
  // ASSERT(trailing_crlf_consumed);

  // There shouldn't be second CRLF present in received command. If so, do not process this command.
  // const ssize_t crlf = buffer.search(CRLF, CRLF.size(), 0, max_line);
  // int length = buffer.length();

  // New code:

  /* size_t crlfPos = data.search(SmtpUtils::CRLF.data(), SmtpUtils::CRLF.size(), 0,
  kMaxCommandLine); if(crlfPos == std::string::npos) {
    // Received data that does not end with /r/n.
    // But we also check if length of data is more than allowed limit.
    if(data.length() >= SmtpUtils::maxCommandLen) {
       return SmtpUtils::Result::ProtocolError;
    }
    return SmtpUtils::Result::NeedMoreData;
   }

  if(crlfPos <= 0) {
    return SmtpUtils::Result::ProtocolError;
  }

  if (crlfPos != data.length() - 2) {
    // Invalid command, duplicate CRLF found
    data.drain(data.length());
    return SmtpUtils::Result::ProtocolError;
  }
  // Extract the command up to CRLF
  std::string buffer = data.toString();
  std::string commandStr = buffer.substr(0, crlfPos);
*/

  std::string current_line;
  command.len = data.length();
  // result = getLine(data, kMaxCommandLine, line);
  result = isValidSmtpLine(data, SmtpUtils::maxCommandLen, current_line);
  if (result != SmtpUtils::Result::ReadyForNext) {
    return result;
  }
  if (current_line.length() != data.length()) {
    return SmtpUtils::Result::ProtocolError;
  }
  // std::string commandStr = current_line.substr(0, crlfPos);
  absl::string_view commandStr = StringUtil::cropRight(current_line, SmtpUtils::CRLF);
  // Split the command into verb and arguments
  size_t spacePos = commandStr.find(' ');
  // std::string verb;
  // std::string args;
  command.verb = (spacePos != std::string::npos) ? commandStr.substr(0, spacePos) : commandStr;
  command.args = (spacePos != std::string::npos) ? commandStr.substr(spacePos + 1) : "";

  /*
  absl::string_view line_remaining(line);

  // Case when \r\n is not at the end of line e.g. mulitple \r\n or some text after \r\n.
  if (line_remaining.size() != data.length()) {
    data.drain(data.length());
    return SmtpUtils::Result::ProtocolError;
  }

  const bool trailing_crlf_consumed = absl::ConsumeSuffix(&line_remaining, SmtpUtils::CRLF);
  ASSERT(trailing_crlf_consumed);

  const size_t space = line_remaining.find(' ');
  std::string verb;
  std::string args;
  if (space == absl::string_view::npos) {
    // no arguments e.g.
    // NOOP\r\n
    verb = std::string(line_remaining);
  } else {
    // EHLO mail.example.com\r\n
    verb = std::string(line_remaining.substr(0, space));
    args = std::string(line_remaining.substr(space + 1));
  }
  */
  // command.verb = verb;
  // command.args = args;
  ENVOY_LOG(debug, "command verb {}", command.verb);
  ENVOY_LOG(debug, "command args {}", command.args);


  data.drain(data.length());
  return result;
}

SmtpUtils::Result DecoderImpl::isValidSmtpLine(Buffer::Instance& data, size_t max_len,
                                               std::string& output) {
  size_t crlfPos = data.search(SmtpUtils::CRLF.data(), SmtpUtils::CRLF.size(), 0, max_len);
  if (crlfPos == std::string::npos) {
    // Received data that does not contain /r/n, possibly received incomplete data.
    // But we also check if length of received data is more than allowed limit.
    if (data.length() >= max_len) {
      return SmtpUtils::Result::ProtocolError;
    }
    return SmtpUtils::Result::NeedMoreData;
  }

  if (crlfPos <= 0) {
    return SmtpUtils::Result::ProtocolError;
  }

  std::string buffer = data.toString();
  output = buffer.substr(0, crlfPos + SmtpUtils::CRLF.size());
  return SmtpUtils::Result::ReadyForNext;
}

SmtpUtils::Result DecoderImpl::parseResponse(Buffer::Instance& data, Response& response) {
  ENVOY_LOG(debug, "smtp_proxy: decoding response {} bytes", data.length());
  ENVOY_LOG(debug, "smtp_proxy: decoding response {}", data.toString());

  int response_code = 0;
  std::string response_msg;
  size_t respose_len = 0;
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  Buffer::OwnedImpl buffer(data);
  // Loop to parse multi-line response
  // https://www.rfc-editor.org/rfc/rfc5321.html#section-4.2
  //  Reply-line     = *( Reply-code "-" [ textstring ] CRLF )
  //                 Reply-code [ SP textstring ] CRLF
  //  Reply-code     = %x32-35 %x30-35 %x30-39
  while (true) {
    std::string current_line;
    result = isValidSmtpLine(buffer, SmtpUtils::maxResponseLen, current_line);
    if (result != SmtpUtils::Result::ReadyForNext) {
      return result;
    }
    buffer.drain(current_line.size());
    respose_len += current_line.length();
    // A response has to be of minimum 3 char length i.e response code needs to be present
    if (current_line.length() < 3) {
      return SmtpUtils::Result::ProtocolError;
    }

    std::string response_code_str = current_line.substr(0, 3);
    int code = 0;
    std::string msg;
    try {
      code = stoi(response_code_str);
    } catch (...) {
      code = 0;
      ENVOY_LOG(error, "smtp_proxy: error while decoding response code ", response_code);
      return SmtpUtils::Result::ProtocolError;
    }
    if (response_code && code != response_code) {
      return SmtpUtils::Result::ProtocolError;
    }
    response_code = code;
    current_line = current_line.erase(0, 3);
    // Separator can be either ' ' or '-'
    char separator = ' ';
    if (!current_line.empty() && current_line != SmtpUtils::CRLF.data()) {
        separator = current_line[0];
        if (separator != ' ' && separator != '-') {
            return SmtpUtils::Result::ProtocolError;
        }
        // Remove the first character from line
        current_line = current_line.erase(0, 1);
        response_msg += current_line;
    }

    if(separator == ' ') {
      break; // We reached last line of reply.
    }

  }

  response.len = respose_len;
  size_t crlf_pos = response_msg.length() - 2;
  response_msg = response_msg.erase(crlf_pos);
  // ENVOY_LOG(debug, "smtp_proxy: response code {}", response_code);
  // ENVOY_LOG(debug, "smtp_proxy: response msg {}",response_msg);
  response.resp_code = response_code;
  response.msg = response_msg;
  // response.len = respose_len;
  data.drain(data.length());
  return SmtpUtils::Result::ReadyForNext;
}

// SmtpUtils::Result DecoderImpl::parseResponse(Buffer::Instance& data, Response& response) {
//   ENVOY_LOG(debug, "smtp_proxy: decoding response {} bytes", data.length());
//   ENVOY_LOG(debug, "smtp_proxy: received response {}", data.toString());

//   SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

//   std::string line;
//   result = getLine(data, SmtpUtils::maxResponseLen, line);
//   if (result != SmtpUtils::Result::ReadyForNext) {
//     return result;
//   }

//   // https://www.rfc-editor.org/rfc/rfc5321.html#section-4.2
//   //  Reply-line     = *( Reply-code "-" [ textstring ] CRLF )
//   //                 Reply-code [ SP textstring ] CRLF
//   //  Reply-code     = %x32-35 %x30-35 %x30-39
//   if (line.length() < 3) {
//     data.drain(data.length());
//     return SmtpUtils::Result::ProtocolError;
//   }

//   std::string response_code_str = line.substr(0, 3);
//   int response_code = 0;
//   std::string response_msg;
//   try {
//     response_code = stoi(response_code_str);
//   } catch (...) {
//     response_code = 0;
//     ENVOY_LOG(error, "smtp_proxy: error while decoding response code ", response_code);
//     data.drain(data.length());
//     return SmtpUtils::Result::ProtocolError;
//   }

//   absl::string_view line_remaining(line);

//   const bool trailing_crlf_consumed = absl::ConsumeSuffix(&line_remaining, SmtpUtils::CRLF);
//   ASSERT(trailing_crlf_consumed);

//   line_remaining.remove_prefix(3);
//   char sp_or_dash = ' ';
//   if (!line_remaining.empty() && line_remaining != SmtpUtils::CRLF) {
//     sp_or_dash = line_remaining[0];
//     if (sp_or_dash != ' ' && sp_or_dash != '-') {
//       data.drain(data.length());
//       return SmtpUtils::Result::ProtocolError;
//     }
//     line_remaining.remove_prefix(1);
//     absl::StrAppend(&response_msg, line_remaining);
//   }
//   response.response_code = response_code;
//   response.msg = response_msg;
//   // result = session_->handleResponse(response_code, response_msg);
//   data.drain(data.length());
//   return result;

//   /*
//   std::string response = data.toString();
//   if (!absl::EndsWith(response, SmtpUtils::smtpCrlfSuffix)) {
//     result = SmtpUtils::Result::NeedMoreData;
//     return result;
//   }

//   response = StringUtil::cropRight(response, SmtpUtils::smtpCrlfSuffix);
//   int length = response.length();
//   if (length < 3) {
//     // Minimum 3 byte response code needed to parse response from server.
//     data.drain(data.length());
//     return result;
//   }
//   std::string response_code_str = response.substr(0, 3);
//   uint16_t response_code = 0;
//   try {
//     response_code = stoi(response_code_str);
//   } catch (...) {
//     response_code = 0;
//     ENVOY_LOG(error, "smtp_proxy: error while decoding response code ", response_code);
//   }
//   if (response_code >= 400 && response_code < 500) {
//     callbacks_->inc4xxErrors();
//   } else if (response_code >= 500 && response_code <= 599) {
//     callbacks_->inc5xxErrors();
//   }
//   // Special handling to parse any error response to connection request.
//   if (!(session_->isCommandInProgress()) &&
//       session_->getState() != SmtpSession::State::ConnectionRequest) {
//     data.drain(data.length());
//     return result;
//   }
//  */
// }

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy