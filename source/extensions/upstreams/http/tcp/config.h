#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

/**
 * Config registration for the Tcp conn pool.
 * @see FIXME.
 */
class TcpGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  ~TcpGenericConnPoolFactory() override = default;
  std::string name() const override { return HttpUpstreamsNames::get().Tcp; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool() const override;
};

DECLARE_FACTORY(TcpGenericConnPoolFactory);

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
