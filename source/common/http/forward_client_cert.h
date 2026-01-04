#pragma once

#include <vector>

#include "envoy/matcher/matcher.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Http {

// Forward declarations - actual types are defined in conn_manager_config.h.
enum class ForwardClientCertType;
enum class ClientCertDetailsType;

/**
 * Interface for forward client cert matcher actions. This allows the conn_manager_utility
 * to access the config from the matched action without depending on the HCM extension.
 * Inherits from Matcher::Action to support getTyped<> without dynamic_cast.
 */
class ForwardClientCertActionConfig : public Matcher::Action {
public:
  /**
   * @return the forward client cert type from this action config.
   */
  virtual ForwardClientCertType forwardClientCertType() const PURE;

  /**
   * @return the set of client cert details to include.
   */
  virtual const std::vector<ClientCertDetailsType>& setCurrentClientCertDetails() const PURE;
};

/**
 * Base class for forward client cert matcher actions that implements the interface.
 */
template <class ProtoType>
class ForwardClientCertActionBase
    : public Matcher::ActionBase<ProtoType, ForwardClientCertActionConfig> {};

} // namespace Http
} // namespace Envoy
