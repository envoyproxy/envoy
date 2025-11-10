#include "source/common/upstream/prod_cluster_info_factory.h"

#include "envoy/stats/scope.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

ClusterInfoConstSharedPtr
ProdClusterInfoFactory::createClusterInfo(const CreateClusterInfoParams& params) {
  Envoy::Stats::ScopeSharedPtr scope =
      params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));

  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      params.server_context_, *scope, params.server_context_.messageValidationVisitor());

  // TODO(JimmyCYJ): Support SDS for HDS cluster.
  Network::UpstreamTransportSocketFactoryPtr socket_factory = THROW_OR_RETURN_VALUE(
      Upstream::createTransportSocketFactory(params.cluster_, factory_context),
      Network::UpstreamTransportSocketFactoryPtr);
  OptRef<const xds::type::matcher::v3::Matcher> matcher;
  if (params.cluster_.has_transport_socket_matcher()) {
    matcher = makeOptRefFromPtr(&params.cluster_.transport_socket_matcher());
  }
  auto socket_matcher = THROW_OR_RETURN_VALUE(
      TransportSocketMatcherImpl::create(params.cluster_.transport_socket_matches(), matcher,
                                         factory_context, socket_factory, *scope),
      std::unique_ptr<TransportSocketMatcherImpl>);

  return THROW_OR_RETURN_VALUE(
      ClusterInfoImpl::create(params.server_context_.initManager(), params.server_context_,
                              params.cluster_, params.bind_config_,
                              params.server_context_.runtime(), std::move(socket_matcher),
                              std::move(scope), params.added_via_api_, factory_context),
      std::unique_ptr<ClusterInfoImpl>);
}

} // namespace Upstream
} // namespace Envoy
