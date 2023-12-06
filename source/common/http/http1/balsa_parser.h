#pragma once

#include <memory>

#include "source/common/http/http1/parser.h"

#include "absl/base/attributes.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/balsa_frame.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/balsa/balsa_visitor_interface.h"

namespace Envoy {
namespace Http {
namespace Http1 {

// This class wraps BalsaFrame and BalsaHeaders into a Parser implementation
// to be used by ConnectionImpl.
class BalsaParser : public Parser, public quiche::BalsaVisitorInterface {
public:
  BalsaParser(MessageType type, ParserCallbacks* connection, size_t max_header_length,
              bool enable_trailers, bool allow_custom_methods);
  ~BalsaParser() override = default;

  // Http1::Parser implementation
  size_t execute(const char* slice, int len) override;
  void resume() override;
  CallbackResult pause() override;
  ParserStatus getStatus() const override;
  Http::Code statusCode() const override;
  bool isHttp11() const override;
  absl::optional<uint64_t> contentLength() const override;
  bool isChunked() const override;
  absl::string_view methodName() const override;
  absl::string_view errorMessage() const override;
  int hasTransferEncoding() const override;

private:
  // quiche::BalsaVisitorInterface implementation
  // TODO(bnc): Encapsulate in a private object.
  void OnRawBodyInput(absl::string_view input) override;
  void OnBodyChunkInput(absl::string_view input) override;
  void OnHeaderInput(absl::string_view input) override;
  void OnHeader(absl::string_view key, absl::string_view value) override;
  void OnTrailerInput(absl::string_view input) override;
  void OnTrailers(std::unique_ptr<quiche::BalsaHeaders> trailers) override;
  void ProcessHeaders(const quiche::BalsaHeaders& headers) override;
  void ProcessTrailers(const quiche::BalsaHeaders& /*trailer*/) override{};
  void OnRequestFirstLineInput(absl::string_view line_input, absl::string_view method_input,
                               absl::string_view request_uri,
                               absl::string_view version_input) override;
  void OnResponseFirstLineInput(absl::string_view line_input, absl::string_view version_input,
                                absl::string_view status_input,
                                absl::string_view reason_input) override;
  void OnChunkLength(size_t chunk_length) override;
  void OnChunkExtensionInput(absl::string_view input) override;
  void OnInterimHeaders(std::unique_ptr<quiche::BalsaHeaders> headers) override;
  void HeaderDone() override;
  void ContinueHeaderDone() override;
  void MessageDone() override;
  void HandleError(quiche::BalsaFrameEnums::ErrorCode error_code) override;
  void HandleWarning(quiche::BalsaFrameEnums::ErrorCode error_code) override;

  // Shared implementation for ProcessHeaders() and OnTrailers().
  void validateAndProcessHeadersOrTrailersImpl(const quiche::BalsaHeaders& headers, bool trailers);

  // Return ParserStatus::Error if `result` is CallbackResult::Error.
  // Return current value of `status_` otherwise.
  // Typical use would be `status_ = convertResult(result);`
  ABSL_MUST_USE_RESULT ParserStatus convertResult(CallbackResult result) const;

  quiche::BalsaFrame framer_;
  quiche::BalsaHeaders headers_;

  const MessageType message_type_ = MessageType::Request;
  ParserCallbacks* connection_ = nullptr;
  const bool enable_trailers_ = false;
  const bool allow_custom_methods_ = false;
  bool first_byte_processed_ = false;
  bool headers_done_ = false;
  ParserStatus status_ = ParserStatus::Ok;
  // An error message, often seemingly arbitrary to match http-parser behavior.
  absl::string_view error_message_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
