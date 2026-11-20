Fixed a bug in Scoped RDS (SRDS) where a single invalid scope in an update batch aborted processing of the
remaining scopes in the push and left already-processed scopes in the main-thread map without propagating
them to worker threads. When enabled, valid scopes in a partially rejected push are applied to worker threads
while failed scopes are skipped and NACKed. Guarded by runtime guard
``envoy.reloadable_features.srds_partial_acceptance`` (disabled by default).
