Fixed a bug in TLS where a false positive ``IS_ENVOY_BUG`` assertion was triggered when a
connection was torn down while asynchronous certificate selection was still in progress.
