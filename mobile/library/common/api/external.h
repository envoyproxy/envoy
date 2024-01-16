#pragma once

#include <string>

namespace Envoy {
namespace Api {
namespace External {

/**
 * Register an external runtime API for usage (e.g. in extensions).
 */
void registerApi(std::string name, void* api);

/**
 * Retrieve an external runtime API for usage (e.g. in extensions).
 */
void* retrieveApi(std::string name, bool allow_absent = false);

} // namespace External
} // namespace Api
} // namespace Envoy
