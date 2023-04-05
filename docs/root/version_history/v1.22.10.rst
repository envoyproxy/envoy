1.22.10 (April 4, 2023)
=======================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* http: validate upstream request header names and values. The new runtime flag ``envoy.reloadable_features.validate_upstream_headers`` can be used for revert this behavior.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* grpc: when Envoy was configured to use ext_authz, ext_proc, tap, ratelimit filters, and grpc access log service and an http header with non-UTF-8 data was received, Envoy would generate an invalid protobuf message and send it to the configured service. The receiving service would typically generate an error when decoding the protobuf message. For ext_authz that was configured with ``failure_mode_allow: true``, the request would have been allowed in this case. For the other services, this could have resulted in other unforseen errors such as a lack of visibility into requests (eg request not logged). Envoy will now by default sanitize the values sent in gRPC service calls to be valid UTF-8, replacing data that is not valid UTF-8 with a '!' character. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.service_sanitize_non_utf8_strings`` to false.
* http: fixed a bug where ``x-envoy-original-path`` was not being sanitized when sent from untrusted users. This behavioral change can be temporarily reverted by setting ``envoy.reloadable_features.sanitize_original_path`` to false.
* http: stop forwarding ``:method`` value which is not a valid token defined in https://www.rfc-editor.org/rfc/rfc9110#section-5.6.2.
  Also, reject ``:method`` and ``:scheme`` headers with multiple values.
* http3: reject pseudo headers violating RFC 9114. Specifically, pseudo-header fields with more than one value for the ``:method`` (non-``CONNECT``),
  ``:scheme``, and ``:path``; or pseudo-header fields after regular header fields; or undefined pseudo-headers.
* lua: lua coroutine should not execute after local reply is sent.
* oauth: fixed a bug where the oauth2 filter would crash if it received a redirect URL without a state query param set.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
