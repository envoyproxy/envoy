#pragma once

#include "envoy/grpc/rpc_channel.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"

namespace Grpc {

/**
 * Concrete implementation of RpcChannel. This is not the most optimal interface but it's the
 * best thing that works with the protoc generated generic rpc code. A code generator plugin
 * would be optimal but that is total overkill.
 *
 * How to use:
 * 1) Add "option cc_generic_services = true;" to proto definition.
 * 2) Use the generated "Stub" service wrapper and pass an RpcChannelImpl to the constructor.
 * 3) The service wrapper can be used to make a call for a single RPC. It can then be reused
 *    to make another call. If parallel calls need to be made, higher level interfaces will be
 *    needed.
 * 4) Inflight RPCs can be safely cancelled using cancel().
 * 5) See GrpcRequestImplTest for an example.
 */
class RpcChannelImpl : public RpcChannel, public Http::AsyncClient::Callbacks {
public:
  RpcChannelImpl(Upstream::ClusterManager& cm, const std::string& cluster,
                 RpcChannelCallbacks& callbacks, const Optional<std::chrono::milliseconds>& timeout)
      : cm_(cm), cluster_(cm.get(cluster)->info()), callbacks_(callbacks), timeout_(timeout) {}

  ~RpcChannelImpl() { ASSERT(!http_request_ && !grpc_method_ && !grpc_response_); }

  static Buffer::InstancePtr serializeBody(const proto::Message& message);

  // Grpc::RpcChannel
  void cancel() override;

  // proto::RpcChannel
  void CallMethod(const proto::MethodDescriptor* method, proto::RpcController* controller,
                  const proto::Message* grpc_request, proto::Message* grpc_response,
                  proto::Closure* done_callback) override;

private:
  void incStat(bool success);
  void onComplete();
  void onFailureWorker(const Optional<uint64_t>& grpc_status, const std::string& message);
  void onSuccessWorker(Http::Message& http_response);

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& http_response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoPtr cluster_;
  Http::AsyncClient::Request* http_request_{};
  const proto::MethodDescriptor* grpc_method_{};
  proto::Message* grpc_response_{};
  RpcChannelCallbacks& callbacks_;
  Optional<std::chrono::milliseconds> timeout_;
};

} // Grpc
