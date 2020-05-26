#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

/**
 * Config registration for the Http conn pool.
 * @see FIXME.
 */
class HttpGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  ~HttpGenericConnPoolFactory() override = default;
  std::string name() const override { return HttpUpstreamsNames::get().Http; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool(HttpOrTcpPool pool) const override;
};

DECLARE_FACTORY(HttpGenericConnPoolFactory);

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
