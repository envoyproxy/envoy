Fixed a bug where a listener or cluster update that changed an SDS ``sds_config`` (for example
adding or removing ``initial_fetch_timeout``) without changing the secret created a new secret
provider that never received the secret over ADS. The new provider hit its initial fetch timeout and
the listener went active without a certificate, failing all TLS handshakes; with an initial fetch
timeout of zero it stayed warming instead. The same could happen for a warming provider created for
a secret already prefetched by on-demand SDS. A new provider now reuses the secret already held by
an existing provider for the same name when both use ADS or the same config source. This behavior
can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.sds_reuse_secret_across_providers`` to ``false``.
