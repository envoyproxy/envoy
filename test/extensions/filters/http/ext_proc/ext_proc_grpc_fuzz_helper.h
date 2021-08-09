#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "source/common/common/thread.h"
#include "source/common/grpc/common.h"

#include "test/common/http/common.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

#include "grpc++/server_builder.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::config::core::v3::HeaderValue;
using envoy::config::core::v3::HeaderValueOption;
using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::CommonResponse;
using envoy::service::ext_proc::v3alpha::HeaderMutation;
using envoy::service::ext_proc::v3alpha::ImmediateResponse;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;
using envoy::type::v3::StatusCode;

const uint32_t ext_proc_fuzz_max_data_size = 1024;
const uint32_t ext_proc_fuzz_max_stream_chunks = 50;

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

enum class HeaderSendSetting { Default, Send, Skip, kMaxValue = Skip };

enum class BodySendSetting {
  None,
  Buffered,
  Streamed,
  BufferedPartial,
  kMaxValue = BufferedPartial
};

// Helper class for fuzzing the ext_proc filter.
// This class exposes functions for randomizing fields of ProcessingResponse
// messages and sub-messages. Further, this class exposes wrappers for
// FuzzedDataProvider functions enabling it to be used safely across multiple
// threads (e.g., in the fuzzer thread and the external processor thread).
class ExtProcFuzzHelper {
public:
  ExtProcFuzzHelper(FuzzedDataProvider* provider);

  StatusCode randomHttpStatus();
  std::string consumeRepeatedString();
  grpc::StatusCode randomGrpcStatusCode();
  grpc::Status randomGrpcStatusWithMessage();

  void randomizeHeaderMutation(HeaderMutation* headers, ProcessingRequest* req, bool trailers);
  void randomizeCommonResponse(CommonResponse* msg, ProcessingRequest* req);
  void randomizeImmediateResponse(ImmediateResponse* msg, ProcessingRequest* req);
  void randomizeOverrideResponse(ProcessingMode* msg);
  void randomizeResponse(ProcessingResponse* resp, ProcessingRequest* req);

  FuzzedDataProvider* provider_;

  // Protects immediate_resp_sent_
  Thread::MutexBasicLockable immediate_resp_lock_;
  // Flags if an immediate response was generated and sent
  bool immediate_resp_sent_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
