#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

#include <vector>

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

static const absl::string_view CRLF = "\r\n";

// https://www.rfc-editor.org/rfc/rfc5321.html#section-4.5.3.1.4
const size_t kMaxCommandLine = 512;

// https://www.rfc-editor.org/rfc/rfc5321.html#section-4.5.3.1.5
const size_t kMaxReplyLine = 512;

Decoder::Result GetLine(Buffer::Instance& data,
			size_t start,
			std::unique_ptr<char[]>& raw_line,
			absl::string_view& line,
			size_t max_line) {
  ssize_t crlf = data.search(CRLF.data(), CRLF.size(), start, max_line);
  if (crlf == -1) {
    if (data.length() >= 512) {
      return Decoder::Result::Bad;
    } else {
      return Decoder::Result::NeedMoreData;
    }
  }
  ASSERT(crlf >= 0);
  // empty line is invalid
  if (static_cast<size_t>(crlf) == start) {
    return Decoder::Result::Bad;
  }

  size_t len = crlf + CRLF.size() - start;
  ASSERT(len > CRLF.size());
  raw_line.reset(new char[len]);
  data.copyOut(start, len, raw_line.get());
  line = absl::string_view(raw_line.get(), len);

  return Decoder::Result::ReadyForNext;
}

Decoder::Result DecoderImpl::DecodeCommand(Buffer::Instance& data, Command& result) {
  std::unique_ptr<char[]> raw_line;
  absl::string_view line;
  Result res = GetLine(data, 0, raw_line, line, kMaxCommandLine);
  if (res != Result::ReadyForNext) return res;

  result.wire_len = line.size();

  ASSERT(absl::ConsumeSuffix(&line, CRLF));

  ssize_t space = line.find(' ');
  if (space == -1) {
    // no arguments e.g.
    // NOOP\r\n
    result.raw_verb = std::string(line);
  } else {
    // EHLO mail.example.com\r\n
    result.raw_verb = std::string(line.substr(0, space));
    result.rest = std::string(line.substr(space + 1));
  }

  static const struct { Command::Verb verb; absl::string_view raw_verb; } kCommands[] = {
    {Decoder::Command::HELO, "HELO"},
    {Decoder::Command::EHLO, "EHLO"},
    {Decoder::Command::STARTTLS, "STARTTLS"},
  };

  for (const auto& c : kCommands) {
    if (absl::AsciiStrToUpper(result.raw_verb) != c.raw_verb) continue;
    result.verb = c.verb;
    break;
  }
  
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
    Result res = GetLine(data, line_off, raw_line, line, kMaxReplyLine);
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
    if (!line.empty() && line != CRLF) {
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

void DecoderImpl::AddEsmtpCapability(absl::string_view cap, std::string& caps_in) {
  std::string caps_out;
  absl::string_view caps = caps_in;
  int i = 0;

  while (!caps.empty()) {
    size_t crlf = caps.find(CRLF);
    if (crlf == absl::string_view::npos) break;
    absl::string_view line = caps.substr(0, crlf + CRLF.size());
    caps = caps.substr(crlf+2);
    if (i > 0) {
      // trim off xyz-
      absl::string_view line_cap = line.substr(4);
      if (absl::StartsWithIgnoreCase(line_cap, cap)) return;
    }
    if (caps.empty()) {
      std::string line_out(line);
      line_out[3] = '-';
      absl::StrAppend(&caps_out, line_out, line_out.substr(0,3), " ", cap, "\r\n");
      break;
    }

    absl::StrAppend(&caps_out, line);
    ++i;
  }

  caps_in = std::move(caps_out);
}

void DecoderImpl::RemoveEsmtpCapability(absl::string_view cap, std::string& caps_in) {
  std::string caps_out;
  absl::string_view caps = caps_in;
  bool first = true;
  size_t last = 0;
  while (!caps.empty()) {
    size_t crlf = caps.find(CRLF);
    if (crlf == absl::string_view::npos) break;
    absl::string_view line = caps.substr(0, crlf + CRLF.size());
    caps = caps.substr(crlf + CRLF.size());
    if (!first) {
      // trim off xyz-
      absl::string_view line_cap = line.substr(4);
      if (absl::StartsWithIgnoreCase(line_cap, cap)) continue;
    }
    last = caps_out.size();
    absl::StrAppend(&caps_out, line);
    first = false;
  }
  caps_out[last+3] = ' ';
  caps_in = std::move(caps_out);
}

bool DecoderImpl::HasEsmtpCapability(absl::string_view cap, absl::string_view caps) {
  bool first = true;
  while (!caps.empty()) {
    size_t crlf = caps.find("\r\n");
    if (crlf == absl::string_view::npos) break;
    absl::string_view line = caps.substr(0, crlf);
    caps = caps.substr(crlf + CRLF.size());
    if (first) {
      first = false;
      continue;
    }

    // trim off xyz-
    line = line.substr(4);
    if (absl::StartsWithIgnoreCase(line, cap)) return true;
  }
  return false;
}


} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
