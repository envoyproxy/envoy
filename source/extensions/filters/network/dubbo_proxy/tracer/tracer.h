#include "envoy/common/pure.h"
#include "envoy/tracing/http_tracer.h"
#include "source/common/http/conn_manager_config.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

class Config : public virtual Tracing::Config {
public:
  virtual Tracing::HttpTracerSharedPtr tracer() const PURE;
  virtual const Http::TracingConnectionManagerConfig* tracingConfig() const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
