//go:build envoy_dynamic_modules_go_custom_bootstrap

package main

import "C"
import "unsafe"

//export envoy_dynamic_module_on_bootstrap_extension_config_destroy
func envoy_dynamic_module_on_bootstrap_extension_config_destroy(config unsafe.Pointer) {}
