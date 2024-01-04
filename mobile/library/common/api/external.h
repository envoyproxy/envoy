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
 * Retrieve an external runtime API for usage (e.g. in extensions). Crashes if the API is not found.
 */
void* retrieveApi(std::string name, bool allow_absent = false);

/**
 * Retrieve an external runtime API for usage (e.g. in extensions). Returns nullptr if the API is
 * not found. Used this method only if `retrieveApi` doesn't work for your use case.
 */
void* retrieveApiSafe(std::string name);

} // namespace External
} // namespace Api
} // namespace Envoy
