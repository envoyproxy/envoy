#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"

#include "common/common/logger.h"
#include "common/grpc/transcoder_input_stream_impl.h"

#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/util/internal/type_info.h"
#include "google/protobuf/util/type_resolver.h"
#include "grpc_transcoding/path_matcher.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Grpc {

// VariableBinding specifies a value for a single field in the request message.
// When transcoding HTTP/REST/JSON to gRPC/proto the request message is
// constructed using the HTTP body and the variable bindings (specified through
// request url).
struct VariableBinding {
  // The location of the field in the protobuf message, where the value
  // needs to be inserted, e.g. "shelf.theme" would mean the "theme" field
  // of the nested "shelf" message of the request protobuf message.
  std::vector<std::string> field_path;
  // The value to be inserted.
  std::string value;
};

class TranscodingConfig : public Logger::Loggable<Logger::Id::config> {
public:
  TranscodingConfig(const Json::Object& config);

  google::protobuf::util::Status
  createTranscoder(const Http::HeaderMap& headers,
                   google::protobuf::io::ZeroCopyInputStream& request_input,
                   google::grpc::transcoding::TranscoderInputStream& response_input,
                   std::unique_ptr<google::grpc::transcoding::Transcoder>& transcoder,
                   const google::protobuf::MethodDescriptor*& method_descriptor);

  google::protobuf::util::Status
  methodToRequestInfo(const google::protobuf::MethodDescriptor* method,
                      google::grpc::transcoding::RequestInfo* info);

private:
  google::protobuf::DescriptorPool descriptor_pool_;
  google::grpc::transcoding::PathMatcherPtr<const google::protobuf::MethodDescriptor*>
      path_matcher_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
};

typedef std::shared_ptr<TranscodingConfig> TranscodingConfigSharedPtr;

class TranscodingFilter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::http2> {
public:
  TranscodingFilter(TranscodingConfig& config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // Http::StreamFilterBase
  void onDestroy() override {}

private:
  bool readToBuffer(google::protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& data);

  TranscodingConfig& config_;
  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_;
  TranscoderInputStreamImpl response_in_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  const google::protobuf::MethodDescriptor* method_{nullptr};
  Http::HeaderMap* response_headers_{nullptr};

  bool error_{false};
};

} // namespace Grpc
} // namespace Envoy
