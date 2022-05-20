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

// ParserStatus is used both to represent the internal state of the parser,
// and by the ParserCallbacks implementation to send signals to the parser.
enum class ParserStatus {
  // An error has happened and execution should be halted.
  Error = -1,
  // Operation successful.
  Success = 0,
  // Returned by onHeadersComplete() to indicate that the parser should not
  // expect a body.
  NoBody = 1,
  // Returned by onHeadersComplete() to indicate that the parser should not
  // expect either a body or any further data on the connection.
  NoBodyData = 2,
  // Returned by Parser::getStatus() to indicate that the parser is paused.
  Paused,
  // Other. This could be returning from a parser code that does not map to the above.
  Unknown,
};

class ParserCallbacks {
public:
  virtual ~ParserCallbacks() = default;
  /**
   * Called when a request/response is beginning.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onMessageBegin() PURE;

  /**
   * Called when URL data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onUrl(const char* data, size_t length) PURE;

  /**
   * Called when response status data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onStatus(const char* data, size_t length) PURE;

  /**
   * Called when header field data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onHeaderField(const char* data, size_t length) PURE;

  /**
   * Called when header value data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onHeaderValue(const char* data, size_t length) PURE;

  /**
   * Called when headers are complete. A base routine happens first then a
   * virtual dispatch is invoked. Note that this only applies to headers and NOT
   * trailers. End of trailers are signaled via onMessageCompleteBase().
   * @return ParserStatus::Error, ParserStatus::Success, ParserStatus::NoBody,
   * or ParserStatus::NoBodyData.
   */
  virtual ParserStatus onHeadersComplete() PURE;

  /**
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length
   */
  virtual void bufferBody(const char* data, size_t length) PURE;

  /**
   * Called when the HTTP message has completed parsing.
   * @return ParserStatus representing success or failure.
   */
  virtual ParserStatus onMessageComplete() PURE;

  /**
   * Called when accepting a chunk header.
   */
  virtual void onChunkHeader(bool) PURE;
};

class Parser {
public:
  virtual ~Parser() = default;

  // Executes the parser.
  // @return the number of parsed bytes.
  virtual size_t execute(const char* slice, int len) PURE;

  // Unpauses the parser.
  virtual void resume() PURE;

  // Pauses the parser and returns a status indicating success.
  virtual ParserStatus pause() PURE;

  // Returns a ParserStatus representing the internal state of the parser.
  virtual ParserStatus getStatus() PURE;

  // Returns an integer representing the status code stored in the parser structure. For responses
  // only.
  // TODO(asraa): Return Envoy::Http::Code.
  virtual uint16_t statusCode() const PURE;

  // Returns an integer representing the HTTP major version.
  virtual int httpMajor() const PURE;

  // Returns an integer representing the HTTP minor version.
  virtual int httpMinor() const PURE;

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
