#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/repeated_field.h>
#include <grpcpp/channel.h>
#include <grpcpp/grpcpp.h>

#include <memory>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_json_transcoder/async_reflection_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

/**
 * This class contains the logic for building a protobuf descriptor pool from a
 * set of FileDescriptorProtos.
 */
class DescriptorPoolBuilder : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Constructor for the non-reflection usage.
   */
  DescriptorPoolBuilder(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      Api::Api& api)
      : proto_config_(proto_config), api_(&api) {}

  /**
   * Constructor. The async_reflection_fetcher is injected rather than
   * constructed internally for easier unit testing.
   */
  DescriptorPoolBuilder(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      AsyncReflectionFetcherSharedPtr async_reflection_fetcher)
      : proto_config_(proto_config), async_reflection_fetcher_(async_reflection_fetcher) {}

  /**
   * Ask this DescriptorPoolBuilder to compute a descriptor pool and call the given callback
   * when the descriptor pool is ready.
   */
  virtual void requestDescriptorPool(
      std::function<void(std::unique_ptr<Protobuf::DescriptorPool>&&)> descriptor_pool_available);

  /**
   * Convert the given FileDescriptorSet into a descriptor pool and pass it
   * into the descriptor_pool_available function passed to
   * requestDescriptorPool.
   */
  virtual void
  loadFileDescriptorProtos(const Envoy::Protobuf::FileDescriptorSet& file_descriptor_set);
  virtual ~DescriptorPoolBuilder() {}

private:
  /**
   * A copy of the original proto_config.
   */
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config_;

  /**
   * A pointer to our AsyncReflectionFetcher if we are using reflection. Set to nullptr if we are
   * not using reflection.
   */
  AsyncReflectionFetcherSharedPtr async_reflection_fetcher_;

  /**
   * A function to call when the requested descriptor pool is available.
   * This is only necessary for when using reflection, although we use it
   * everywhere for consistency.
   */
  std::function<void(std::unique_ptr<Protobuf::DescriptorPool>&&)> descriptor_pool_available_;

  /**
   * API used for reading from the filesystem when loading protos from the filesystem.
   */
  Api::Api* api_;
};

using DescriptorPoolBuilderSharedPtr = std::shared_ptr<DescriptorPoolBuilder>;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
