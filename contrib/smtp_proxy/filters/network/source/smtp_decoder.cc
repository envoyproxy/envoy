#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

#include <vector>

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

Decoder::Result GetLine(Buffer::Instance& data,
			size_t off,
			std::unique_ptr<char[]>& raw_line,
			absl::string_view& line) {
  ssize_t crlf = data.search("\r\n", 2, off, data.length() - off);
  if (crlf == -1) {
    if (data.length() >= 1024) {
      return Decoder::Result::Bad;
    } else {
      return Decoder::Result::NeedMoreData;
    }
  }
  ASSERT(crlf >= 0);
  if (static_cast<size_t>(crlf) == off) {
    return Decoder::Result::Bad;
  }

  size_t len = crlf+2 - off;
  ASSERT(len >= 2);
  raw_line.reset(new char[len]);
  data.copyOut(off, len, raw_line.get());
  line = absl::string_view(raw_line.get(), len);

  return Decoder::Result::ReadyForNext;
}

Decoder::Result DecoderImpl::DecodeCommand(Buffer::Instance& data, Command& result) {
  std::unique_ptr<char[]> raw_line;
  absl::string_view line;
  Result res = GetLine(data, 0, raw_line, line);
  if (res != Result::ReadyForNext) return res;

  result.wire_len = line.size();

  ASSERT(absl::ConsumeSuffix(&line, "\r\n"));

  ssize_t space = line.find(' ');
  if (space == -1) {
    // no arguments e.g.
    // NOOP\r\n
    result.verb = std::string(line);
  } else {
    // EHLO mail.example.com\r\n
    result.verb = std::string(line.substr(0, space));
    result.rest = std::string(line.substr(space + 1));
  }

  absl::AsciiStrToLower(&result.verb);
  
  // could be a little more persnickety about at least verb must be only printing chars, etc.

  return Result::ReadyForNext;
}

Decoder::Result DecoderImpl::DecodeResponse(Buffer::Instance& data, Response& result) {
  std::unique_ptr<char[]> raw_line;

  std::optional<int> code;
  size_t line_off = 0;
  std::string msg;
  for (;;) {
    absl::string_view line;
    Result res = GetLine(data, line_off, raw_line, line);
    if (res != Result::ReadyForNext) return res;
    line_off += line.size();
    // https://www.rfc-editor.org/rfc/rfc5321.html#section-4.2
    //  Reply-line     = *( Reply-code "-" [ textstring ] CRLF )
    //                 Reply-code [ SP textstring ] CRLF
    //  Reply-code     = %x32-35 %x30-35 %x30-39
    if (line.size() < 3) return Result::Bad;
    for (int i = 0; i < 3; ++i) {
      if (!absl::ascii_isdigit(line[i])) return Result::Bad;
    }
    absl::string_view code_str = line.substr(0,3);
    int this_code;
    ASSERT(absl::SimpleAtoi(code_str, &this_code));

    if (code && this_code != *code) return Result::Bad;
    code = this_code;

    line = line.substr(3);
    char sp_or_dash = ' ';
    if (!line.empty() && line != "\r\n") {
      sp_or_dash = line[0];
      if (sp_or_dash != ' ' && sp_or_dash != '-') return Result::Bad;
      line = line.substr(1);
      absl::StrAppend(&msg, line);
    }

    // ignore enhanced code for now

    if (sp_or_dash == ' ') break;  // last line of multiline
  }
  if (!code) return Result::Bad;  // assert
  result.code = *code;
  result.msg = msg;
  result.wire_len = line_off;
  return Result::ReadyForNext;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
