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

// The following define special return values for parser callbacks.
// These codes do not overlap with standard HTTP Status codes. They are only used for user
// callbacks.
enum class ParserStatus {
  // Callbacks other than on_headers_complete should return a non-zero int to indicate an error
  // and
  // halt execution.
  Error = -1,
  Success = 0,
  // Returning '1' from on_headers_complete will tell http_parser that it should not expect a
  // body.
  NoBody = 1,
  // Returning '2' from on_headers_complete will tell http_parser that it should not expect a body
  // nor any further data on the connection.
  NoBodyData = 2,
  // Pause parser.
  Paused,
  // Other. This could be returning from a parser code that does not map to the above.
  Unknown,
};

class ParserCallbacks {
public:
  virtual ~ParserCallbacks() = default;
  /**
   * Called when a request/response is beginning.
   * @return integer return code from the parser indicating status.
   */
  virtual Status onMessageBegin() PURE;

  /**
   * Called when URL data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return Status representing success or failure.
   */
  virtual Status onUrl(const char* data, size_t length) PURE;

  /**
   * Called when header field data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return Status representing success or failure.
   */
  virtual Status onHeaderField(const char* data, size_t length) PURE;

  /**
   * Called when header value data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return Status representing success or failure.
   */
  virtual Status onHeaderValue(const char* data, size_t length) PURE;

  /**
   * Called when response status data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   * @return Status representing success or failure.
   */
  virtual Status onStatus(const char* data, size_t length) PURE;

  /**
   * Called when headers are complete. A base routine happens first then a virtual dispatch is
   * invoked. Note that this only applies to headers and NOT trailers. End of
   * trailers are signaled via onMessageCompleteBase().
   * @return An error status or a ParserStatus.
   */
  virtual Envoy::StatusOr<ParserStatus> onHeadersComplete() PURE;

  /**
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length
   */
  virtual void bufferBody(const char* data, size_t length) PURE;

  /**
   * Called when the HTTP message has completed parsing.
   * @return An error status or a ParserStatus.
   */
  virtual StatusOr<ParserStatus> onMessageComplete() PURE;

  /**
   * Called when accepting a chunk header.
   */
  virtual void onChunkHeader(bool) PURE;

  virtual int setAndCheckCallbackStatus(Status&& status) PURE;
  virtual int setAndCheckCallbackStatusOr(Envoy::StatusOr<ParserStatus>&& statusor) PURE;
};

class Parser {
public:
  // Struct containing the return value from parser execution.
  struct RcVal {
    // Number of parsed bytes.
    size_t nread;
    // Integer error from parser indicating return code.
    int rc;
  };
  virtual ~Parser() = default;

  // Executes the parser.
  // @return an RcVal containing the number of parsed bytes and return code.
  virtual RcVal execute(const char* slice, int len) PURE;

  // Unpauses the parser.
  virtual void resume() PURE;

  // Pauses the parser and returns a status indicating pause.
  virtual ParserStatus pause() PURE;

  // Returns a parser status representing the errno value from the parser.
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

  // Returns a textual representation of the given return code.
  virtual absl::string_view errnoName(int rc) const PURE;

  // Returns whether the Transfer-Encoding header is present.
  virtual int hasTransferEncoding() const PURE;

  // Converts a ParserStatus code to the parsers' integer return code value.
  virtual int statusToInt(const ParserStatus code) const PURE;
};

using ParserPtr = std::unique_ptr<Parser>;

} // namespace Http1
} // namespace Http
} // namespace Envoy
