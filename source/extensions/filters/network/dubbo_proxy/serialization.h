#pragma once

#include <string>
#include <unordered_map>

#include "envoy/buffer/buffer.h"

#include "common/common/macros.h"
#include "common/singleton/const_singleton.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Names of available serialization implementations.
 */
class SerializerNameValues {
public:
  // hessian serialization
  const std::string HESSIAN = "hessian";
  // json serialization
  const std::string JSON = "json";
};

typedef ConstSingleton<SerializerNameValues> SerializerNames;

/**
 * RpcInvocation represent an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcInvocation.java
 */
class RpcInvocation {
public:
  virtual ~RpcInvocation() {}
  virtual const std::string& getMethodName() const PURE;
  virtual const std::string& getServiceName() const PURE;
  virtual const std::string& getServiceVersion() const PURE;
};

typedef std::unique_ptr<RpcInvocation> RpcInvocationPtr;

/**
 * RpcResult represent the result of an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcResult.java
 */
class RpcResult {
public:
  virtual ~RpcResult() {}
  virtual bool hasException() const PURE;
};

typedef std::unique_ptr<RpcResult> RpcResultPtr;

class SerializationCallbacks {
public:
  virtual ~SerializationCallbacks() {}
  virtual void onRpcInvocation(RpcInvocationPtr&& invo) PURE;
  virtual void onRpcResult(RpcResultPtr&& res) PURE;
};

class Serialization {
public:
  virtual ~Serialization() {}
  /**
   * Return this serialization's name
   *
   * @return std::string containing the serialization name.
   */
  virtual const std::string& name() const PURE;
  /**
   * deserialize an rpc call, potentially invoking callbacks
   * If successful, the RpcInvocation removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcInvocation size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual void deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size) PURE;
  /**
   * deserialize result of an rpc call, potentially invoking callbacks
   * If successful, the RpcResult removed from the buffer
   *
   * @param buffer the currently buffered dubbo data
   * @body_size the complete RpcResult size
   * @throws EnvoyException if the data is not valid for this serialization
   */
  virtual void deserializeRpcResult(Buffer::Instance& buffer, size_t body_size) PURE;
};

typedef std::unique_ptr<Serialization> SerializationPtr;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy