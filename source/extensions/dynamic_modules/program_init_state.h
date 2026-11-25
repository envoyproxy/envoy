#pragma once

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * @return whether the envoy_dynamic_module_on_program_init of the loaded shared object succeeded,
 * so that a load that finds the shared object already loaded runs it again only if it failed.
 * @param handle the handle of the shared object.
 */
bool programInitSucceeded(const void* handle);

/**
 * Records whether the envoy_dynamic_module_on_program_init of the loaded shared object succeeded. A
 * load that opens a shared object records that it has not succeeded yet, since a shared object
 * that was unloaded can leave its handle to another one.
 * @param handle the handle of the shared object.
 * @param succeeded whether the init function succeeded.
 */
void setProgramInitSucceeded(const void* handle, bool succeeded);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
