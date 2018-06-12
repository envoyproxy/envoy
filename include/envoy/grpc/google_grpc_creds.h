#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/common/pure.h"

#include "common/config/datasource.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

/**
 * Interface for all Google gRPC credentials factories.
 */
class GoogleGrpcCredentialsFactory {
public:
  virtual ~GoogleGrpcCredentialsFactory() {}

  /**
   * Get a ChannelCredentials to be used for authentication of a gRPC channel.
   *
   * GoogleGrpcCredentialsFactory should always return a ChannelCredentials. To use CallCredentials,
   * the ChannelCredentials can be created by using a combination of CompositeChannelCredentials and
   * CompositeCallCredentials to combine multiple credentials.
   *
   * @param grpc_service_config contains configuration options
   * @return std::shared_ptr<grpc::ChannelCredentials> to be used to authenticate a Google gRPC
   * channel.
   */
  virtual std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of
   * a Google gRPC credentials factory.
   */
  virtual std::string name() const PURE;
};

} // namespace Grpc
} // namespace Envoy
