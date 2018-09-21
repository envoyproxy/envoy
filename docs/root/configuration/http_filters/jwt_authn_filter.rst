.. _config_http_filters_jwt_authn:

JWT Authentication
==================

JSON Web Token (JWT) usually is used to carry end user identity. This build-in HTTP filter can be used to authenticate JWT.

A public key (JWKS) is needed to verify a JWT signature. It can be specified in the filter config, or can be fetched remotely from a JWKS server. If it is fetched remotely, Envoy will cache it.

If JWT verification fails, the request will be rejected. If JWT verification success, its payload can be forwarded to the upstream if desired.

Configuration
-------------

Filter config uses "rules" to specify matching rules and their authentication requirements. It uses "providers" map to specify how a JWT should be verified, such as where to extract the token, where to fetch the public key (JWKS) and how to output its payload.

The external authorization HTTP filter calls an external gRPC or HTTP service to check if the incoming
HTTP request is authorized or not.
If the request is deemed unauthorized then the request will be denied normally with 403 (Forbidden) response.
Note that sending additional custom metadata from the authorization service to the upstream, or to the downstream is 
also possible. This is explained in more details at :ref:`HTTP filter <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`.

.. tip::
  It is recommended that this filter is configured first in the filter chain so that requests are
  authorized prior to the rest of filters processing the request.

The content of the requests that are passed to an authorization service is specified by 
:ref:`CheckRequest <envoy_api_msg_service.auth.v2alpha.CheckRequest>`.

.. _config_http_filters_ext_authz_http_configuration:

The HTTP filter, using a gRPC/HTTP service, can be configured as follows. You can see all the
configuration options at
:ref:`HTTP filter <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`.

Configuration Examples
-----------------------------

A sample filter configuration for a gRPC authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.ext_authz
      config:
        grpc_service:
           envoy_grpc:
             cluster_name: ext-authz

.. code-block:: yaml

  clusters:
    - name: ext-authz
      type: static
      http2_protocol_options: {}
      hosts:
        - socket_address: { address: 127.0.0.1, port_value: 10003 }

A sample filter configuration for a raw HTTP authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.ext_authz
      config:
        http_service:
            server_uri:
              uri: 127.0.0.1:10003
              cluster: ext-authz
              timeout: 0.25s
              failure_mode_allow: false
  
.. code-block:: yaml
  
  clusters:
    - name: ext-authz
      connect_timeout: 0.25s
      type: logical_dns
      lb_policy: round_robin
      hosts:
        - socket_address: { address: 127.0.0.1, port_value: 10003 }

Statistics
----------
The HTTP filter outputs statistics in the *cluster.<route target cluster>.ext_authz.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total responses from the filter.
  error, Counter, Total errors contacting the external service.
  denied, Counter, Total responses from the authorizations service that were to deny the traffic.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of failure_mode_allow set to true."
