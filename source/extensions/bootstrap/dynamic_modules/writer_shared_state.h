#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/xds_manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

/**
 * The state that the writers of a server share. Their apply passes hold back the requests of the
 * xDS types that their resources depend on, and the requests resume together, so that one time
 * limit holds when their bursts overlap.
 */
class WriterSharedState : public Singleton::Instance {
public:
  static std::shared_ptr<WriterSharedState>
  get(Server::Configuration::ServerFactoryContext& context);

  /**
   * Holds back the requests of the xDS types that the resources of the type depend on, unless an
   * earlier pass holds them back already.
   */
  void pauseDependentTypes(absl::string_view resource_type_url, Config::XdsManager& xds_manager,
                           MonotonicTime now);

  /**
   * Records whether the writer applied its burst of writes. Resumes the held back requests once no
   * writer has a burst to apply, or about one second after they were first held back.
   */
  void onApplyPassDone(const void* writer, bool burst_applied, MonotonicTime now);

  /**
   * Called when the writer shuts down. Resumes the held back requests once no writer has a burst to
   * apply.
   */
  void onShutdown(const void* writer);

private:
  // The addresses of the writers whose burst of writes is not fully applied yet.
  absl::flat_hash_set<const void*> writers_applying_;
  // The held back requests per resource type URL, which share one start time.
  absl::flat_hash_map<std::string, Config::ScopedResume> dependent_type_pauses_;
  MonotonicTime dependent_type_pause_start_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
