#pragma once

#include "source/common/memory/heap_shrinker.h"
#include "source/server/server.h"

namespace Envoy {
namespace Server {

// The production server instance, which creates all of the required components.
class InstanceImpl : public InstanceBase {
public:
  using InstanceBase::InstanceBase;

protected:
  void maybeCreateHeapShrinker() override;
  absl::StatusOr<std::unique_ptr<OverloadManager>> createOverloadManager() override;
  std::unique_ptr<OverloadManager> createNullOverloadManager() override;
  std::unique_ptr<Server::GuardDog> maybeCreateGuardDog(absl::string_view name) override;
  std::unique_ptr<HdsDelegateApi>
  maybeCreateHdsDelegate(Configuration::ServerFactoryContext& server_context, Stats::Scope& scope,
                         Grpc::RawAsyncClientPtr&& async_client, Envoy::Stats::Store& stats,
                         Ssl::ContextManager& ssl_context_manager,
                         Upstream::ClusterInfoFactory& info_factory) override;

private:
  std::unique_ptr<Memory::HeapShrinker> heap_shrinker_;
};

} // namespace Server
} // namespace Envoy
