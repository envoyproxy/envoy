.. _config_http_filters_jwt_authn:

JWT Authentication
==================

This HTTP filter can be used to authenticate JSON Web Token (JWT). If JWT verification fails, the request will be rejected. If JWT verification success, its payload can be forwarded to the upstream for further authorization.

A public key (JWKS) is needed to verify a JWT signature. It can be specified in the filter config, or can be fetched remotely from a JWKS server. If it is fetched remotely, Envoy will cache it.

Configuration
-------------

This HTTP filer config has two fields, "providers" and "rules". Field "providers" specifies how JWT should be verified, such as where to extract the token, where to fetch the public key (JWKS) and how to output its payload. Field "rules" specifies matching rules and their requirements. It a request matches a rule, its requirement applies. The requirement specifies which JWT providers should be verified.

JwtProvider
~~~~~~~~~~~

It specifies how JWT should be verified. It has following fields:

* issuer: the principal that issued the JWT, usually a URL or an email address
* audiences: a list of JWT audiences allowed to access. A JWT containing any of these audiences will be accepted.
  If not specified, will not check audiences in the JWT
* local_jwks: fetch JWKS in local data source, either in a local file or embedded in the inline string.
* remote_jwks: fetch JWKS from a remote HTTP server, also specify cache duration.
* forward: if true, JWT will be forwarded to the upstream.
* from_headers: extract JWT from custom HTTP header other than the default location.
* from_params: extract JWT from custom query parameter keys other than the default location.
* forward_payload_header: forward the JWT payload in the specified HTTP header.

The default token extract location: if "from_headers" and "from_params" is empty,  the default location is from HTTP header::

  Authorization: Bearer <token>

If fails to extract a token from there, then check query parameter key "access_token" as this example::
  
  /path?access_token=<JWT>

In the filter config, "providers" a map, to map provider_name to a JwtProvider message. The "provider_name" has to be unique, usually it is the same as "issuer".  Multiple JwtProviders with the same issuer are allowed to specify different audiences or different token extract locations for the same issuer.


Config examples:

.. code-block:: yaml

  providers:
    https://example.com:
      issuer: https://example.com
      audiences:
      - bookstore_android.apps.googleusercontent.com
      - bookstore_web.apps.googleusercontent.com
      remote_jwks:
        http_uri:
          uri: https://example.com/.well-known/jwks.json
          cluster: example_jwks_cluster
        cache_duration:
          seconds: 300

Above example specifies:

* provider_name is "https://example.com"
* issuer is https://example.com also.
* audiences: bookstore_android.apps.googleusercontent.com and bookstore_web.apps.googleusercontent.com
* remote_jwks: use URL "https://example.com/.well-known/jwks.json" from cluster "example_jwks_cluster", its cache duration is 300 seconds.
* token will be extracted from the default extract locations since from_headers and from_params are empty.
* token will not be forwarded to upstream
* JWT payload will not be added to the request header.

.. code-block:: yaml

  providers:
    https://example2.com:
      issuer: https://example2.com
      local_jwks:
        inline_string: "PUBLIC-KEY"
      from_headers:
      - name: x-goog-iap-jwt-assertion
      forward: true
      forward_payload_header: x-jwt-payload

Above example specifies:

* provider_name is "https://example2.com"
* issuer is https://example2.com too.
* audiences: not specified, JWT "aud" field will not be checked.
* local_jwks: JWKS is embeded in the inline string.
* from_headers: token will be extracted from HTTP headers as::

     x-goog-iap-jwt-assertion: <JWT>.
  
* forward: token will forwarded to upstream
* JWT payload will be added to the request header as following format::

    x-jwt-payload: base64_encoded(jwt_payload_in_JSON)
  

RequirementRule
~~~~~~~~~~~~~~~

It has two fields: "match" and "requires".  The field "match" to specify how a request can be matched; e.g. by HTTP headers, or by query parameters, or by path prefixes.  The field "requires" specifies the JWT requirement, e.g. which provider is required. Multiple providers may be required in such forms, such as "require_all" or "require_any". 

The field "match" uses following fields to define a match:
* one of following path_specifier: prefix, path, and regex.
* headers: specify how to match HTTP headers.
* query_parameters: specify how to match query parameters.

The field "requires" can be specified as any one of followings:
* provider_name: specifies the provider name of required JwtProvider
* provider_and_audiences: specifies the provider with audiences. The audiences will override the one in the JwtProvider.
* requires_any: a list of requirements that if any of them success, it will be success.
* requires_all: a list of requirements that only if all of them success, it will be success.
* allow_missing_or_failed: If true, all JWT token will be verified, successfully verified JWTs will output its payload results. The request will proceeded regardless JWT is missing or any of verification failures. The typical use case is: there is another HTTP filter after this JWT filter. The JWT filter is used to do JWT verification, that filter will make decision based on the results.




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
