dynamic modules: fixed a bug where the built-in Hickory DNS resolver aborted the process when it
could not create its Tokio runtime or resolver, for example under resource exhaustion. The module
now reports the failure and Envoy rejects the configuration with an error instead of aborting.
