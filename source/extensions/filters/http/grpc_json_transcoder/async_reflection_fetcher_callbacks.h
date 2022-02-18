#pragma once

#include <memory>

#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/protobuf.h"

#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

using ServerReflectionResponsePtr =
    std::unique_ptr<grpc::reflection::v1alpha::ServerReflectionResponse>;

/**
 * Instances of this class receive callbacks from gRPC reflection Rpcs.
 * This class is entirely inlined because all functions are short.
 */
class AsyncReflectionFetcherCallbacks
    : public Envoy::Grpc::AsyncStreamCallbacks<grpc::reflection::v1alpha::ServerReflectionResponse>,
      public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Constructor.
   */
  AsyncReflectionFetcherCallbacks(
      std::function<void(AsyncReflectionFetcherCallbacks*, ServerReflectionResponsePtr&&)>
          on_success_callback)
      : on_success_callback_(on_success_callback) {}

  /**
   * Callback for a successful Server Reflection response.
   */
  void onReceiveMessage(ServerReflectionResponsePtr&& message) {
    on_success_callback_(this, std::move(message));
  }

  /**
   * See RawAsyncStreamCallbacks for documentation of these methods.
   */
  void onCreateInitialMetadata(Http::RequestHeaderMap&) {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&){};

  /**
   * These two are used for detecting unexpected errors.
   */
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Envoy::Grpc::Status::GrpcStatus status, const std::string& message) override {
    if (status != Envoy::Grpc::Status::Ok) {
      ENVOY_LOG(error, "transcoding_filter: server reflection request failed with status {}: {}",
                status, message);
    }
  }

private:
  std::function<void(AsyncReflectionFetcherCallbacks*, ServerReflectionResponsePtr&&)>
      on_success_callback_;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
