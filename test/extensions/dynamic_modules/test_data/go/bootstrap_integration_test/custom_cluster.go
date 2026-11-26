//go:build envoy_dynamic_modules_go_custom_cluster

package main

import "C"
import "unsafe"

//export envoy_dynamic_module_on_cluster_config_destroy
func envoy_dynamic_module_on_cluster_config_destroy(config unsafe.Pointer) {}
