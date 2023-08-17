#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

#include <vector>

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

namespace {

static constexpr absl::string_view CRLF = "\r\n";

// While https://www.rfc-editor.org/rfc/rfc5321.html#section-4.5.3.1.4
// prescribes a max command line of 512 bytes, individual SMTP
// extensions may increase this. e.g. extensions that add an
// additional keyword to the MAIL FROM: command typically increase the
// max mail command by the length of that keyword. For BINARYMIME
// (rfc3030) this is 16 bytes, for SMTPUTF8 (rfc6531), it is 8
// bytes. To the letter of the rfc, you need to understand all of the
// extensions advertised by the server to determine the exact
// limit. This is not a full implementation of SMTP; the limit here is
// meant as a circuit breaker against obvious garbage. The real
// implementation on either side is free to enforce a more precise
// limit.
constexpr size_t kMaxCommandLine = 1024;
// Likewise, the rfc reply limit is 512 bytes
// https://www.rfc-editor.org/rfc/rfc5321.html#section-4.5.3.1.5
constexpr size_t kMaxReplyLine = 1024;

// The smtp proxy filter only expects to consume the first ~3
// responses from the upstream (greeting, ehlo, starttls), which
// should be much smaller than this but again, don't buffer an
// unbounded amount.
constexpr size_t kMaxReplyBytes = 65536;
} // anonymous namespace

Decoder::Result getLine(const Buffer::Instance& data, size_t max_line,
                        std::string& line_out) {
  const ssize_t crlf = data.search(CRLF.data(), CRLF.size(), 0, max_line);
  if (crlf == -1) {
    if (data.length() >= max_line) {
      return Decoder::Result::ProtocolError;
    } else {
      return Decoder::Result::NeedMoreData;
    }
  }
  ASSERT(crlf >= 0);
  // empty line is invalid
  if (crlf == 0) {
    return Decoder::Result::ProtocolError;
  }

  const size_t len = crlf + CRLF.size();
  ASSERT(len > CRLF.size());
  auto front_slice = data.frontSlice();
  if (front_slice.len_ >= len) {
    line_out.append(static_cast<char*>(front_slice.mem_), len);
  } else {
    std::unique_ptr<char[]> tmp(new char[len]);
    data.copyOut(0, len, tmp.get());
    line_out.append(tmp.get(), len);
  }
  return Decoder::Result::ReadyForNext;
}

Decoder::Result DecoderImpl::decodeCommand(const Buffer::Instance& data, Command& result) {
  std::string line;
  Result res = getLine(data, kMaxCommandLine, line);
  if (res != Result::ReadyForNext) {
    return res;
  }
  absl::string_view line_remaining(line);
  result.wire_len = line_remaining.size();

  const bool trailing_crlf = absl::ConsumeSuffix(&line_remaining, CRLF);
  ASSERT(trailing_crlf);

  const size_t space = line_remaining.find(' ');
  if (space == absl::string_view::npos) {
    // no arguments e.g.
    // NOOP\r\n
    result.raw_verb = std::string(line_remaining);
  } else {
    // EHLO mail.example.com\r\n
    result.raw_verb = std::string(line_remaining.substr(0, space));
    result.rest = std::string(line_remaining.substr(space + 1));
  }

  static const struct {
    Command::Verb verb;
    absl::string_view raw_verb;
  } kCommands[] = {
      {Decoder::Command::Helo, "HELO"},
      {Decoder::Command::Ehlo, "EHLO"},
      {Decoder::Command::Starttls, "STARTTLS"},
  };

  for (const auto& c : kCommands) {
    if (absl::AsciiStrToUpper(result.raw_verb) != c.raw_verb) {
      continue;
    }
    result.verb = c.verb;
    break;
  }

  // We aren't too picky here since we're only going to look at the
  // first ~2 commands (EHLO, STARTTLS) and the filter is going to
  // hang up if the first isn't EHLO.
  return Result::ReadyForNext;
}

Decoder::Result DecoderImpl::decodeResponse(const Buffer::Instance& data, Response& result) {
  Buffer::OwnedImpl data_remaining(data);
  absl::optional<int> code;
  std::string msg;
  size_t total_reply = 0;
  for (;;) {
    std::string line;
    const Result res = getLine(data_remaining, kMaxReplyLine, line);
    if (res != Result::ReadyForNext) {
      return res;
    }
    data_remaining.drain(line.size());
    absl::string_view line_remaining(line);
    total_reply += line_remaining.size();
    if (total_reply > kMaxReplyBytes) {
      return Result::ProtocolError;
    }
    // https://www.rfc-editor.org/rfc/rfc5321.html#section-4.2
    //  Reply-line     = *( Reply-code "-" [ textstring ] CRLF )
    //                 Reply-code [ SP textstring ] CRLF
    //  Reply-code     = %x32-35 %x30-35 %x30-39
    if (line_remaining.size() < 3) {
      return Result::ProtocolError;
    }
    for (int i = 0; i < 3; ++i) {
      if (!absl::ascii_isdigit(line_remaining[i])) {
        return Result::ProtocolError;
      }
    }
    absl::string_view code_str = line_remaining.substr(0, 3);
    int this_code = 0;
    const bool atoi_result = absl::SimpleAtoi(code_str, &this_code);
    ASSERT(atoi_result);

    if (code && this_code != *code) {
      return Result::ProtocolError;
    }
    code = this_code;

    line_remaining.remove_prefix(3);
    char sp_or_dash = ' ';
    if (!line_remaining.empty() && line_remaining != CRLF) {
      sp_or_dash = line_remaining[0];
      if (sp_or_dash != ' ' && sp_or_dash != '-') {
        return Result::ProtocolError;
      }
      line_remaining.remove_prefix(1);
      absl::StrAppend(&msg, line_remaining);
    }

    // ignore enhanced code for now

    if (sp_or_dash == ' ') {
      break; // last line of multiline
    }
  }
  ASSERT(code);  // we returned ProtocolError by now if we didn't populate code
  result.code = *code;
  result.msg = msg;
  result.wire_len = total_reply;
  return Result::ReadyForNext;
}

// line is
// 234-SIZE 1048576\r\n
bool matchCapability(absl::string_view line, absl::string_view cap) {
  line.remove_prefix(4); // 234-
  line.remove_suffix(CRLF.size());
  if (!absl::StartsWithIgnoreCase(line, cap)) {
    return false;
  }
  line = line.substr(cap.size());
  if (line.empty() || line[0] == ' ') {
    return true;
  }
  return false;
}

void DecoderImpl::addEsmtpCapability(absl::string_view cap, std::string& caps) {
  std::string caps_out;
  absl::string_view caps_remaining = caps;
  bool first = true;

  while (!caps_remaining.empty()) {
    const size_t crlf = caps_remaining.find(CRLF);
    if (crlf == absl::string_view::npos) {
      break; // invalid input
    }
    absl::string_view line = caps_remaining.substr(0, crlf + CRLF.size());
    caps_remaining.remove_prefix(crlf + CRLF.size());
    if (!first && matchCapability(line, cap)) {
      return;
    }
    first = false;
    if (caps_remaining.empty()) {
      std::string line_out(line);
      line_out[3] = '-';
      absl::StrAppend(&caps_out, line_out, line_out.substr(0, 3), " ", cap, "\r\n");
      break;
    }

    absl::StrAppend(&caps_out, line);
  }

  caps = std::move(caps_out);
}

void DecoderImpl::removeEsmtpCapability(absl::string_view cap, std::string& caps) {
  std::string caps_out;
  absl::string_view caps_remaining = caps;
  bool first = true;
  size_t last = 0;
  while (!caps_remaining.empty()) {
    const size_t crlf = caps_remaining.find(CRLF);
    if (crlf == absl::string_view::npos) {
      return; // invalid input
    }
    absl::string_view line = caps_remaining.substr(0, crlf + CRLF.size());
    caps_remaining.remove_prefix(crlf + CRLF.size());
    if (!first && matchCapability(line, cap)) {
      continue;
    }
    first = false;
    last = caps_out.size();
    absl::StrAppend(&caps_out, line);
  }
  caps_out[last + 3] = ' ';
  caps = std::move(caps_out);
}

bool DecoderImpl::hasEsmtpCapability(absl::string_view cap, absl::string_view caps_remaining) {
  bool first = true;
  while (!caps_remaining.empty()) {
    const size_t crlf = caps_remaining.find("\r\n");
    if (crlf == absl::string_view::npos) {
      break; // invalid input
    }
    absl::string_view line = caps_remaining.substr(0, crlf + CRLF.size());
    caps_remaining.remove_prefix(crlf + CRLF.size());
    if (first) {
      first = false;
      continue;
    }

    if (matchCapability(line, cap)) {
      return true;
    }
  }
  return false;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
