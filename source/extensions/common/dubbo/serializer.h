#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/config/typed_config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Serializer {
public:
  virtual ~Serializer() = default;

  virtual SerializeType type() const PURE;

  /**
   * Deserialize an rpc call. If successful, the RpcRequest removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @param context context information for RPC messages
   * @return a pair containing the deserialized result of the message and the deserialized
   *         invocation information.
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual RpcRequestPtr deserializeRpcRequest(Buffer::Instance& buffer, Context& context) PURE;

  /**
   * deserialize result of an rpc call
   *
   * @param buffer the currently buffered dubbo data
   * @param context context information for RPC messages
   * @return a pair containing the deserialized result of the message and the deserialized
   *         result information.
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual RpcResponsePtr deserializeRpcResponse(Buffer::Instance& buffer, Context& context) PURE;

  /**
   * Serialize response of an rpc call
   * If successful, the buffer is written to the serialized data
   *
   * @param buffer store the serialized data
   * @param metadata metadata that contains context information and rpc response.
   */
  virtual void serializeRpcResponse(Buffer::Instance& buffer, MessageMetadata& metadata) PURE;

  /**
   * Serialize request of an rpc call
   * If successful, the buffer is written to the serialized data
   *
   * @param buffer store the serialized data
   * @param metadata metadata that contains context information and rpc request.
   */
  virtual void serializeRpcRequest(Buffer::Instance& buffer, MessageMetadata& metadata) PURE;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
