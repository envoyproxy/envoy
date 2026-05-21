Fixed a bug in the :ref:`mTLS Authenticated <envoy_v3_api_msg_extensions.rbac.principals.mtls_authenticated.v3.Config>`
principal extension where if an invalid SAN matcher was configured, it would incorrectly match any
certificate in the allowed trust chain. Known bad configurations are an invalid ``OID`` when using
an ``OTHER_NAME``, or specifying a ``StringMatcher`` with an invalid configuration.
