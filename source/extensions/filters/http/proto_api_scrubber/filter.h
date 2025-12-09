#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/strings/string_view.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

using proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using proto_processing_lib::proto_scrubber::ProtoScrubber;
using proto_processing_lib::proto_scrubber::ScrubberContext;

inline constexpr const char kFilterName[] = "envoy.filters.http.proto_api_scrubber";

/**
 * A filter that supports scrubbing of request and response protobuf payloads based on configured
 * restrictions.
 */
class ProtoApiScrubberFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  explicit ProtoApiScrubberFilter(const ProtoApiScrubberFilterConfig& filter_config)
      : filter_config_(filter_config) {}

  Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

private:
  // Rejects requests and sends local reply back to the client.
  void rejectRequest(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                     absl::string_view rc_detail);

  // Rejects response and sends local reply back to the client.
  void rejectResponse(Envoy::Grpc::Status::GrpcStatus grpc_status, absl::string_view error_msg,
                      absl::string_view rc_detail);

  // Checks if the method should be blocked based on method-level restrictions.
  // Returns true if the request should be blocked, false otherwise.
  bool checkMethodLevelRestrictions(Envoy::Http::RequestHeaderMap& headers);

  std::shared_ptr<const ProtoApiScrubberFilterConfig> config_;
  bool is_valid_grpc_request_ = false;

  // Request message converter which converts Envoy Buffer data to StreamMessage (for scrubbing) and
  // vice-versa.
  GrpcFieldExtraction::MessageConverterPtr request_msg_converter_{nullptr};

  // Response message converter which converts Envoy Buffer data to StreamMessage (for scrubbing)
  // and vice-versa.
  GrpcFieldExtraction::MessageConverterPtr response_msg_converter_{nullptr};

  // Creates and returns an instance of `ProtoScrubber` which can be used for request scrubbing.
  absl::StatusOr<std::unique_ptr<ProtoScrubber>> createRequestProtoScrubber();

  // Creates and returns an instance of `ProtoScrubber` which can be used for response scrubbing.
  absl::StatusOr<std::unique_ptr<ProtoScrubber>> createResponseProtoScrubber();

  const ProtoApiScrubberFilterConfig& filter_config_;

  // Stores the full gRPC method name e.g., `/package.service/method`.
  // It is populated while decoding the headers (i.e., in the `decodeHeaders()` method) and is used
  // during decoding and encoding of the data (i.e., decodeData(), encodeData(), respectively).
  std::string method_name_;

  // The field checker which uses match tree configured in the filter config to determine whether a
  // field should be preserved or removed from the request protobuf payloads.
  // NOTE: This must outlive `request_scrubber_`, which holds a non-owning reference to this
  // instance.
  std::unique_ptr<FieldCheckerInterface> request_match_tree_field_checker_;

  // The scrubber instance for the request path.
  // It is lazily initialized in decodeData() to ensure it is instantiated exactly
  // once per request, preserving state across multiple data frames (e.g., for
  // gRPC streaming or large payloads).
  std::unique_ptr<ProtoScrubber> request_scrubber_;

  // The field checker which uses match tree configured in the filter config to determine whether a
  // field should be preserved or removed from the response protobuf payloads.
  // NOTE: This must outlive `response_scrubber_`, which holds a non-owning reference to this
  // instance.
  std::unique_ptr<FieldCheckerInterface> response_match_tree_field_checker_;

  // The scrubber instance for the response path.
  // It is lazily initialized in encodeData() to ensure it is instantiated exactly
  // once per request, preserving state across multiple data frames (e.g., for
  // gRPC streaming or large payloads).
  std::unique_ptr<ProtoScrubber> response_scrubber_;
};

class FilterFactory : public Common::FactoryBase<ProtoApiScrubberConfig> {
private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtoApiScrubberConfig& proto_config, const std::string&,
                                    Server::Configuration::FactoryContext&) override;
};
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
