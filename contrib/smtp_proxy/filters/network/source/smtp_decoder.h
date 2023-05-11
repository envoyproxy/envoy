#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Smtp message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  // The following values are returned by the decoder, when filter
  // passes bytes of data via onData method:
  enum class Result {
    ReadyForNext, // Decoder processed previous message and is ready for the next message.
    NeedMoreData, // Decoder needs more data to reconstruct the message.
    Bad
  };

  struct Command {
    enum Verb {
      UNKNOWN,
      HELO,
      EHLO,
      STARTTLS
    };
    Verb verb = UNKNOWN;
    std::string raw_verb;
    std::string rest;
    size_t wire_len;
  };

  struct Response {
    int code;
    std::string msg;
    size_t wire_len;
  };

  // Attempts to decode an SMTP command from data. If the buffer contains a
  // complete command, this is parsed out into result and ReadyForNext
  // is returned. Otherwise:
  // NeedMoreData: buffer contains an incomplete command
  // Bad: buffer contains data that is not a prefix of a valid command
  virtual Result DecodeCommand(Buffer::Instance& data,
			       Command& result) = 0;
  // Attempts to decode an SMTP response from data.
  virtual Result DecodeResponse(Buffer::Instance& data,
				Response& result) = 0;
  
  // The following routines operate on the full wire bytes of a
  // response to EHLO that has previously been validated by
  // DecodeResponse() e.g.
  // 220-example.com smtp server at your service\r\n
  // 220-PIPELINING\r\n
  // 220 STARTTLS\r\n

  // Adds the ESMTP capability cap to caps if not present.
  virtual void AddEsmtpCapability(absl::string_view cap, std::string& caps) = 0;
  // Adds the ESMTP capability cap to caps if present.
  virtual void RemoveEsmtpCapability(absl::string_view cap, std::string& caps) = 0;
  // Returns true if the ESMTP capability cap is present in caps.
  virtual bool HasEsmtpCapability(absl::string_view cap, absl::string_view caps) = 0;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl() = default;

  // if data starts with a valid smtp command, decodes into result and returns ReadyForNext
  // else returns NeedMoreData
  Result DecodeCommand(Buffer::Instance& data,
		       Command& result) override;

  Result DecodeResponse(Buffer::Instance& data,
			Response& result) override;

  void AddEsmtpCapability(absl::string_view, std::string&) override;
  void RemoveEsmtpCapability(absl::string_view, std::string&) override;
  bool HasEsmtpCapability(absl::string_view cap, absl::string_view caps) override;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
