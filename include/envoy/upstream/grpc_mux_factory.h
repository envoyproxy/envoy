#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/grpc/async_client.h"
#include "envoy/event/dispatcher.h"
#include "envoy/config/grpc_mux.h"
#include "common/protobuf/protobuf.h"
#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/runtime/runtime.h"


namespace Envoy {
namespace Config {

class GrpcMuxFactory {
public:
    virtual ~GrpcMuxFactory() {}

    virtual Config::GrpcMux*
    getOrCreateMux(const LocalInfo::LocalInfo &local_info, Grpc::AsyncClientPtr async_client,
                   Event::Dispatcher &dispatcher, const Protobuf::MethodDescriptor &service_method,
                   Runtime::RandomGenerator &random, const ::envoy::api::v2::core::ApiConfigSource& config_source,
                   Stats::Scope& scope, const std::string type_url) PURE;
};

typedef std::unique_ptr<GrpcMuxFactory> GrpcMuxFactoryPtr;
}
}
