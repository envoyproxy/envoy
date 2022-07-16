#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "source/common/common/statusor.h"
#include "source/common/http/status.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * Every parser implementation should have a corresponding parser type here.
 */
enum class ParserType { Legacy };

enum class MessageType { Request, Response };

// CallbackResult is used to send signals to the parser. See
// https://github.com/nodejs/http-parser/blob/5c5b3ac62662736de9e71640a8dc16da45b32503/http_parser.h#L72.
enum class CallbackResult {
  // An error has happened. Further data must not be fed to the parser.
  Error = -1,
  // Operation successful.
  Success = 0,
  // Returned by onHeadersComplete() to indicate that the parser should not
  // expect a body.
  NoBody = 1,
  // Returned by onHeadersComplete() to indicate that the parser should not
  // expect either a body or any further data on the connection.
  NoBodyData = 2,
};

class ParserCallbacks {
public:
  virtual ~ParserCallbacks() = default;
  /**
   * Called when a request/response is beginning.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onMessageBegin() PURE;

  /**
   * Called when URL data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onUrl(const char* data, size_t length) PURE;

  /**
   * Called when response status data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onStatus(const char* data, size_t length) PURE;

  /**
   * Called when header field data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onHeaderField(const char* data, size_t length) PURE;

  /**
   * Called when header value data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onHeaderValue(const char* data, size_t length) PURE;

  /**
   * Called when headers are complete. A base routine happens first then a
   * virtual dispatch is invoked. Note that this only applies to headers and NOT
   * trailers. End of trailers are signaled via onMessageCompleteBase().
   * @return CallbackResult::Error, CallbackResult::Success,
   * CallbackResult::NoBody, or CallbackResult::NoBodyData.
   */
  virtual CallbackResult onHeadersComplete() PURE;

  /**
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length
   */
  virtual void bufferBody(const char* data, size_t length) PURE;

  /**
   * Called when the HTTP message has completed parsing.
   * @return CallbackResult representing success or failure.
   */
  virtual CallbackResult onMessageComplete() PURE;

  /**
   * Called when accepting a chunk header.
   */
  virtual void onChunkHeader(bool) PURE;
};

// ParserStatus represents the internal state of the parser.
enum class ParserStatus {
  // An error has occurred.
  Error = -1,
  // No error.
  Ok = 0,
  // The parser is paused.
  Paused,
};

class Parser {
public:
  virtual ~Parser() = default;

  // Executes the parser.
  // @return the number of parsed bytes.
  virtual size_t execute(const char* slice, int len) PURE;

  // Unpauses the parser.
  virtual void resume() PURE;

  // Pauses the parser. Returns CallbackResult::Success, which can be returned
  // directly in ParserCallback implementations for brevity.
  virtual CallbackResult pause() PURE;

  // Returns a ParserStatus representing the internal state of the parser.
  virtual ParserStatus getStatus() const PURE;

  // Returns an integer representing the status code stored in the parser structure. For responses
  // only.
  // TODO(asraa): Return Envoy::Http::Code.
  virtual uint16_t statusCode() const PURE;

  // Returns whether HTTP version is 1.1.
  virtual bool isHttp11() const PURE;

  // Returns the number of bytes in the body. absl::nullopt if no Content-Length header
  virtual absl::optional<uint64_t> contentLength() const PURE;

  // Returns whether headers are chunked.
  virtual bool isChunked() const PURE;

  // Returns a textual representation of the method. For requests only.
  virtual absl::string_view methodName() const PURE;

  // Returns a textual representation of the internal error state of the parser.
  virtual absl::string_view errorMessage() const PURE;

  // Returns whether the Transfer-Encoding header is present.
  virtual int hasTransferEncoding() const PURE;
};

using ParserPtr = std::unique_ptr<Parser>;

} // namespace Http1
} // namespace Http
} // namespace Envoy
