Dynamic module logs emitted through ``envoy_dynamic_module_callback_log`` now report the module
source location of the log statement instead of a fixed location inside Envoy. The callback takes
the source file and line as new parameters, so modules must be rebuilt against the updated ABI.
