.. _config_http_filters_jwt_authn:

JWT Authentication
==================

This HTTP filter can be used to verify JSON Web Token (JWT). It will verify its signature, audiences and issuer. It will also check its time restrictions, such as expiration and nbf (not before) time. If the JWT verification fails, its request will be rejected. If the JWT verification succeeds, its payload can be forwarded to the upstream for further authorization if desired.

JWKS is needed to verify JWT signatures. They can be specified in the filter config or can be fetched remotely from a JWKS server.

.. attention::
   Only ES256 and RS256 are supported for the JWT alg.

Configuration
-------------

This filter should be configured with the name *envoy.filters.http.jwt_authn*.

This HTTP :ref:`filter config <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.JwtAuthentication>` has two fields:

* Field *providers* specifies how a JWT should be verified, such as where to extract the token, where to fetch the public key (JWKS) and where to output its payload.
* Field *rules* specifies matching rules and their requirements. If a request matches a rule, its requirement applies. The requirement specifies which JWT providers should be used.

JwtProvider
~~~~~~~~~~~

:ref:`JwtProvider <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.JwtProvider>` specifies how a JWT should be verified. It has the following fields:

* *issuer*: the principal that issued the JWT, usually a URL or an email address.
* *audiences*: a list of JWT audiences allowed to access. A JWT containing any of these audiences will be accepted.
  If not specified, the audiences in JWT will not be checked.
* *local_jwks*: fetch JWKS in local data source, either in a local file or embedded in the inline string.
* *remote_jwks*: fetch JWKS from a remote HTTP server, also specify cache duration.
* *forward*: if true, JWT will be forwarded to the upstream.
* *from_headers*: extract JWT from HTTP headers.
* *from_params*: extract JWT from query parameters.
* *forward_payload_header*: forward the JWT payload in the specified HTTP header.

Default Extract Location
~~~~~~~~~~~~~~~~~~~~~~~~

If *from_headers* and *from_params* is empty,  the default location to extract JWT is from HTTP header::

  Authorization: Bearer <token>

If fails to extract a JWT from above header, then check query parameter key *access_token* as in this example::

  /path?access_token=<JWT>

In the :ref:`filter config <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.JwtAuthentication>`, *providers* is a map, to map *provider_name* to a :ref:`JwtProvider <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.JwtProvider>`. The *provider_name* must be unique, it is referred in the `JwtRequirement <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.JwtRequirement>` in its *provider_name* field.

.. important::
   For *remote_jwks*, a **jwks_cluster** cluster is required.

Due to above requirement, `OpenID Connect Discovery <https://openid.net/specs/openid-connect-discovery-1_0.html>`_ is not supported since the URL to fetch JWKS is in the response of the discovery. It is not easy to setup a cluster config for a dynamic URL.

Remote JWKS config example
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    provider_name1:
      issuer: https://example.com
      audiences:
      - bookstore_android.apps.googleusercontent.com
      - bookstore_web.apps.googleusercontent.com
      remote_jwks:
        http_uri:
          uri: https://example.com/jwks.json
          cluster: example_jwks_cluster
        cache_duration:
          seconds: 300

Above example fetches JWSK from a remote server with URL https://example.com/jwks.json. The token will be extracted from the default extract locations. The token will not be forwarded to upstream. JWT payload will not be added to the request header.

Following cluster **example_jwks_cluster** is needed to fetch JWKS.

.. code-block:: yaml

  cluster:
    name: example_jwks_cluster
    type: STRICT_DNS
    hosts:
      socket_address:
        address: example.com
        port_value: 80


Inline JWKS config example
~~~~~~~~~~~~~~~~~~~~~~~~~~

Another config example using inline JWKS:

.. code-block:: yaml

  providers:
    provider_name2:
      issuer: https://example2.com
      local_jwks:
        inline_string: PUBLIC-KEY
      from_headers:
      - name: jwt-assertion
      forward: true
      forward_payload_header: x-jwt-payload

Above example uses config inline string to specify JWKS. The JWT token will be extracted from HTTP headers as::

     jwt-assertion: <JWT>.

JWT payload will be added to the request header as following format::

    x-jwt-payload: base64_encoded(jwt_payload_in_JSON)

RequirementRule
~~~~~~~~~~~~~~~

:ref:`RequirementRule <envoy_api_msg_config.filter.http.jwt_authn.v2alpha.RequirementRule>` has two fields:

* Field *match* specifies how a request can be matched; e.g. by HTTP headers, or by query parameters, or by path prefixes.
* Field *requires* specifies the JWT requirement, e.g. which provider is required.

.. important::
   - **If a request matches multiple rules, the first matched rule will apply**.
   - If the matched rule has empty *requires* field, **JWT verification is not required**.
   - If a request doesn't match any rules, **JWT verification is not required**.

Single requirement config example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    jwt_provider1:
      issuer: https://example.com
      audiences:
        audience1
      local_jwks:
        inline_string: PUBLIC-KEY
  rules:
  - match:
      prefix: /health
  - match:
      prefix: /api
    requires:
      provider_and_audiences:
        provider_name: jwt_provider1
        audiences:
          api_audience
  - match:
      prefix: /
    requires:
      provider_name: jwt_provider1

Above config uses single requirement rule, each rule may have either an empty requirement or a single requirement with one provider name.

Group requirement config example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    provider1:
      issuer: https://provider1.com
      local_jwks:
        inline_string: PUBLIC-KEY
    provider2:
      issuer: https://provider2.com
      local_jwks:
        inline_string: PUBLIC-KEY
  rules:
  - match:
      prefix: /any
    requires:
      requires_any:
        requirements:
        - provider_name: provider1
        - provider_name: provider2
  - match:
      prefix: /all
    requires:
      requires_all:
        requirements:
        - provider_name: provider1
        - provider_name: provider2

Above config uses more complex *group* requirements:

* The first *rule* specifies *requires_any*; if any of **provider1** or **provider2** requirement is satisfied, the request is OK to proceed.
* The second *rule* specifies *requires_all*; only if both **provider1** and **provider2** requirements are satisfied, the request is OK to proceed.
