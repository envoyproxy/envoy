#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

class GoogleGrpcUtils {
public:
  /**
   * Build grpc::ByteBuffer which aliases the data in a Buffer::InstancePtr.
   * @param bufferInstance source data container.
   * @return byteBuffer target container aliased to the data in Buffer::Instance and owning the
   * Buffer::Instance.
   */
  static grpc::ByteBuffer makeByteBuffer(Buffer::InstancePtr&& bufferInstance);

  /**
   * Build Buffer::Instance which aliases the data in a grpc::ByteBuffer.
   * @param byteBuffer source data container.
   * @param Buffer::InstancePtr target container aliased to the data in grpc::ByteBuffer.
   */
  static Buffer::InstancePtr makeBufferInstance(const grpc::ByteBuffer& byteBuffer);
};

} // namespace Grpc
} // namespace Envoy
