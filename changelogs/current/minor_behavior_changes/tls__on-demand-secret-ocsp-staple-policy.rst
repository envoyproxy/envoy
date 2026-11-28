The ``envoy.tls.certificate_selectors.on_demand_secret`` certificate selector now fails the handshake
when the :ref:`OCSP staple policy
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.ocsp_staple_policy>`
rejects the selected certificate for a client that requests an OCSP response, as the default
certificate selector does, where it used to complete the handshake without a response. This affects a
certificate with an expired OCSP response under ``MUST_STAPLE`` or ``STRICT_STAPLING``, and under any
policy a certificate with the must-staple extension and an expired response, since the extension
upgrades the policy to ``MUST_STAPLE``. Each failure counts in the listener's
``ssl.ocsp_staple_failed``, the only OCSP statistic the selector records, so watch that counter when
upgrading. This behavioral change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.on_demand_secret_enforce_ocsp_staple_policy`` to ``false``.
