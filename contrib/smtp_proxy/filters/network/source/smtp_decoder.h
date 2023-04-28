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

  virtual Result DecodeCommand(Buffer::Instance& data,
			       Command& result) = 0;

  virtual Result DecodeResponse(Buffer::Instance& data,
				Response& result) = 0;
 
protected:

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
 
protected:
  // Buffer used to temporarily store a downstream smtp packet
  // while sending other packets. Currently used only when negotiating
  // upstream SSL.
  Buffer::OwnedImpl temp_storage_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
