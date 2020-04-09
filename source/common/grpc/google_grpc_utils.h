#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/grpc_service.pb.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

class GoogleGrpcUtils {
public:
  /**
   * Build grpc::ByteBuffer which aliases the data in a Buffer::InstancePtr.
   * @param buffer source data container.
   * @return byteBuffer target container aliased to the data in Buffer::Instance and owning the
   * Buffer::Instance.
   */
  static grpc::ByteBuffer makeByteBuffer(Buffer::InstancePtr&& buffer);

  /**
   * Build Buffer::Instance which aliases the data in a grpc::ByteBuffer.
   * @param buffer source data container.
   * @return a Buffer::InstancePtr aliased to the data in the provided grpc::ByteBuffer and
   * owning the corresponding grpc::Slice(s) or nullptr if the grpc::ByteBuffer is bad.
   */
  static Buffer::InstancePtr makeBufferInstance(const grpc::ByteBuffer& buffer);

  /**
   * Build gRPC channel based on the given GrpcService configuration.
   * @param config  Google gRPC config.
   * @param api reference to the Api object
   * @return static std::shared_ptr<grpc::Channel> a gRPC channel.
   */
  static std::shared_ptr<grpc::Channel>
  createChannel(const envoy::config::core::v3::GrpcService& config, Api::Api& api);
};

} // namespace Grpc
} // namespace Envoy
