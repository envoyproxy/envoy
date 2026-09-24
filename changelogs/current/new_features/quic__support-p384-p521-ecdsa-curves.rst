Added support for P-384 and P-521 ECDSA leaf certificates for QUIC downstream connections, in
addition to the previously supported P-256. This can be temporarily reverted by setting the runtime
guard ``envoy.reloadable_features.quic_support_additional_ecdsa_curves`` to ``false``.
