// NOLINT(namespace-envoy)

#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

using ::Envoy::Extensions::Network::SocketInterface::DynamicModules::SocketReadyHandle;

// These are the strong implementations of the socket interface callbacks. They override the
// weak stubs in the central abi_impl.cc when the socket interface extension is used.

extern "C" {

void envoy_dynamic_module_callback_socket_interface_io_ready(
    envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr
        io_ready_handle_module_ptr,
    uint32_t events) {
  if (io_ready_handle_module_ptr == nullptr) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_socket_interface_io_ready called with a null "
                 "handle");
    return;
  }
  static_cast<SocketReadyHandle*>(io_ready_handle_module_ptr)->commit(events);
}

void envoy_dynamic_module_callback_socket_interface_io_ready_delete(
    envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr
        io_ready_handle_module_ptr) {
  if (io_ready_handle_module_ptr == nullptr) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_socket_interface_io_ready_delete called with a "
                 "null handle");
    return;
  }
  delete static_cast<SocketReadyHandle*>(io_ready_handle_module_ptr);
}

} // extern "C"
