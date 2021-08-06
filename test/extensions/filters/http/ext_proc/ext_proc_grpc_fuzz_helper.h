#ifndef TEST_EXTENSIONS_FILTERS_HTTP_EXT_PROC_EXT_PROC_GRPC_FUZZ_HELPER_H_
#define TEST_EXTENSIONS_FILTERS_HTTP_EXT_PROC_EXT_PROC_GRPC_FUZZ_HELPER_H_

#include <mutex>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "grpc++/server_builder.h"

#include "source/common/grpc/common.h"
#include "test/common/http/common.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::CommonResponse;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using envoy::service::ext_proc::v3alpha::HeaderMutation;
using envoy::type::v3::StatusCode;
using envoy::config::core::v3::HeaderValueOption;
using envoy::config::core::v3::HeaderValue;

// TODO(ikepolinsky): integrate an upstream that can be controlled by the fuzzer
// and responds appropriately to HTTP requests.
// Currently using autonomous upstream which sends 10 bytes in response to any
// HTTP message. This is an invalid response to TRACE, HEAD, and PUT requests
// so they are currently not supported. DELETE, PATCH, CONNECT, and OPTIONS
// use the same two send functions as GET and POST but with a different method value
// (e.g., they just use sendDownstreamRequest and sendDownstreamRequestWithBody)
// for simplicity I have excluded anything other than GET and POST for now.
enum class HttpMethod {
  GET,
  POST,
  DELETE,
  PATCH,
  CONNECT,
  OPTIONS,
  TRACE,
  HEAD,
  PUT,
  kMaxValue = POST // TODO(ikepolinsky): update when methods supported
};

enum class ResponseType {
  RequestHeaders,
  ResponseHeaders,
  RequestBody,
  ResponseBody,
  ImmediateResponse,
  RequestTrailers,
  ResponseTrailers,
  kMaxValue = ResponseTrailers
};

enum class HeaderSendSetting {
  Default,
  Send,
  Skip,
  kMaxValue = Skip
};

enum class BodySendSetting {
  None,
  Buffered,
  Streamed,
  BufferedPartial,
  kMaxValue = Streamed // TODO(ikepolinsky): update when BufferedPartial implemented
};

// Helper class for fuzzing the ext_proc filter.
// This class exposes functions for randomizing fields of ProcessingResponse
// messages and sub-messages. Further, this class exposes wrappers for
// FuzzedDataProvider functions enabling it to be used safely across multiple
// threads (e.g., in the fuzzer thread and the external processor thread).
class ExtProcFuzzHelper {
public:

  ExtProcFuzzHelper(FuzzedDataProvider* provider);

  // Wrapper functions for FuzzedDataProvider to make them thread safe
  bool ConsumeBool();
  std::string ConsumeRandomLengthString();
  //template <typename T> T ConsumeIntegralInRange(T min, T max);
  //template <typename T> T ConsumeEnum();

  template <typename T> T ConsumeIntegralInRange(T min, T max) {
    std::unique_lock<std::mutex> lock(provider_lock_);
    return provider_->ConsumeIntegralInRange<T>(min, max);
  }

  template <typename T> T ConsumeEnum() {
    std::unique_lock<std::mutex> lock(provider_lock_);
    return provider_->ConsumeEnum<T>();
  }

  StatusCode RandomHttpStatus();
  grpc::StatusCode RandomGrpcStatusCode();
  grpc::Status RandomGrpcStatusWithMessage();

  void RandomizeHeaderMutation(HeaderMutation* headers, ProcessingRequest* req, bool trailers);
  void RandomizeCommonResponse(CommonResponse* msg, ProcessingRequest* req);
  void RandomizeImmediateResponse(ImmediateResponse* msg, ProcessingRequest* req);
  void RandomizeOverrideResponse(ProcessingMode* msg);
  void RandomizeResponse(ProcessingResponse* resp, ProcessingRequest* req);

  FuzzedDataProvider* provider_;
  // Protects provider_
  std::mutex provider_lock_;

  // Protects immediate_resp_sent_
  std::mutex immediate_resp_lock_;
  // Flags if an immediate response was generated and sent
  bool immediate_resp_sent_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#endif  // TEST_EXTENSIONS_FILTERS_HTTP_EXT_PROC_EXT_PROC_GRPC_FUZZ_HELPER_H_
