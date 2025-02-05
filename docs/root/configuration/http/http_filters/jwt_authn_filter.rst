.. _config_http_filters_jwt_authn:

JWT Authentication
==================

This HTTP filter can be used to verify JSON Web Token (JWT). It will verify its signature, audiences and issuer. It will also check its time restrictions, such as expiration and nbf (not before) time. If the JWT verification fails, its request will be rejected. If the JWT verification succeeds, its payload can be forwarded to the upstream for further authorization if desired.

JWKS is needed to verify JWT signatures. They can be specified in the filter config or can be fetched remotely from a JWKS server.

Following are supported JWT alg:

.. code-block::

   ES256, ES384, ES512,
   HS256, HS384, HS512,
   RS256, RS384, RS512,
   PS256, PS384, PS512,
   EdDSA

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.jwt_authn.v3.JwtAuthentication``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtAuthentication>`

This HTTP :ref:`filter config <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtAuthentication>` has two fields:

* Field ``providers`` specifies how a JWT should be verified, such as where to extract the token, where to fetch the public key (JWKS) and where to output its payload.
* Field ``rules`` specifies matching rules and their requirements. If a request matches a rule, its requirement applies. The requirement specifies which JWT providers should be used.

JwtProvider
~~~~~~~~~~~

:ref:`JwtProvider <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtProvider>` specifies how a JWT should be verified. It has the following fields:

* ``issuer``: the principal that issued the JWT, usually a URL or an email address.
* ``audiences``: a list of JWT audiences allowed to access. A JWT containing any of these audiences will be accepted.
  If not specified, the audiences in JWT will not be checked.
* ``local_jwks``: fetch JWKS in local data source, either in a local file or embedded in the inline string.
* ``remote_jwks``: fetch JWKS from a remote HTTP server, also specify cache duration.
* ``forward``: if true, JWT will be forwarded to the upstream.
* ``from_headers``: extract JWT from HTTP headers.
* ``from_params``: extract JWT from query parameters.
* ``from_cookies``: extract JWT from HTTP request cookies.
* ``forward_payload_header``: forward the JWT payload in the specified HTTP header.
* ``claim_to_headers``: copy JWT claim to HTTP header.
* ``jwt_cache_config``: Enables JWT cache, its size can be specified by ``jwt_cache_size``. Only valid JWT tokens are cached.

Default Extract Location
~~~~~~~~~~~~~~~~~~~~~~~~

If :ref:`from_headers <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.from_headers>` and
:ref:`from_params <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.from_params>` is empty, the default location to extract JWT is from HTTP header::

  Authorization: Bearer <token>

and query parameter key ``access_token`` as::

  /path?access_token=<JWT>

If a request has two tokens, one from the header and the other from the query parameter, all of them must be valid.

The :ref:`providers <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtAuthentication.providers>` field is a map, to map ``provider_name`` to a :ref:`JwtProvider <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtProvider>`. The ``provider_name`` must be unique, it is referred by the fields :ref:`provider_name <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtRequirement.provider_name>` and :ref:`provider_name <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.ProviderWithAudiences.provider_name>`.

.. important::
   If :ref:`remote_jwks <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.remote_jwks>` is used, a ``jwks_cluster`` cluster is required to be specified in the field
   :ref:`cluster <envoy_v3_api_field_config.core.v3.HttpUri.cluster>`.

Due to above requirement, `OpenID Connect Discovery <https://openid.net/specs/openid-connect-discovery-1_0.html>`_ is not supported since the URL to fetch JWKS is in the response of the discovery. It is not easy to setup a cluster config for a dynamic URL.


Token Extraction from Custom HTTP Headers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the JWT needs to be extracted in other HTTP header, use :ref:`from_headers <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.from_headers>` to specify the header name.
In addition to the :ref:`name <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.name>` field, which specifies the HTTP header name, the section can specify an optional :ref:`value_prefix <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.value_prefix>` value, as in:

.. literalinclude:: _include/jwt-authn-headers-filter.yaml
    :language: yaml
    :lines: 41-43
    :lineno-start: 41
    :linenos:
    :caption: :download:`jwt-authn-headers-filter.yaml <_include/jwt-authn-headers-filter.yaml>`

The above will cause the jwt_authn filter to look for the JWT in the ``x-jwt-header`` header, following the tag ``jwt_value``.
Any non-JWT characters (i.e., anything other than alphanumerics, `_`, `-`, and `.`) will be skipped,
and all following, contiguous, JWT-legal chars will be taken as the JWT.

This means all of the following will return a JWT of ``eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk``:

.. code-block:: yaml

    x-jwt-header: jwt_value=eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk

    x-jwt-header: {"jwt_value": "eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk"}

    x-jwt-header: beta:true,jwt_value:"eyJFbnZveSI6ICJyb2NrcyJ9.e30.c2lnbmVk",trace=1234


The header :ref:`name <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.name>` may be ``Authorization``.

The :ref:`value_prefix <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.value_prefix>` must match exactly, i.e., case-sensitively.
If the :ref:`value_prefix <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.value_prefix>` is not found, the header is skipped, not considered as a source for a JWT token.

If there are no JWT-legal characters after the :ref:`value_prefix <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtHeader.value_prefix>`, the entire string after it
is taken to be the JWT token. This is unlikely to succeed; the error will reported by the JWT parser.


Remote JWKS config example
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: _include/jwt-authn-filter.yaml
    :language: yaml
    :lines: 38-49
    :lineno-start: 38
    :linenos:
    :caption: :download:`jwt-authn-filter.yaml <_include/jwt-authn-filter.yaml>`

Above example fetches JWKS from a remote server with URL https://example.com/jwks.json. The token will be extracted from the default extract locations. The token will not be forwarded to upstream. JWT payload will not be added to the request header.

Following cluster **example_jwks_cluster** is needed to fetch JWKS.

.. literalinclude:: _include/jwt-authn-filter.yaml
    :language: yaml
    :lines: 54-71
    :lineno-start: 54
    :linenos:
    :caption: :download:`jwt-authn-filter.yaml <_include/jwt-authn-filter.yaml>`

Inline JWKS config example
~~~~~~~~~~~~~~~~~~~~~~~~~~

Another config example using inline JWKS:

.. literalinclude:: _include/jwt-authn-inline-filter.yaml
    :language: yaml
    :lines: 32-51
    :lineno-start: 32
    :linenos:
    :caption: :download:`jwt-authn-inline-filter.yaml <_include/jwt-authn-inline-filter.yaml>`

Above example uses config inline string to specify JWKS. The JWT token will be extracted from HTTP headers as:

.. code-block::

     jwt-assertion: <JWT>

JWT payload will be added to the request header as following format:

.. code-block::

    x-jwt-payload: base64url_encoded(jwt_payload_in_JSON)

RequirementRule
~~~~~~~~~~~~~~~

:ref:`RequirementRule <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.RequirementRule>` has two fields:

* Field ``match`` specifies how a request can be matched; e.g. by HTTP headers, or by query parameters, or by path prefixes.
* Field ``requires`` specifies the JWT requirement, e.g. which provider is required.

.. important::
   - **If a request matches multiple rules, the first matched rule will apply**.
   - If the matched rule has empty ``requires`` field, **JWT verification is not required**.
   - If a request doesn't match any rules, **JWT verification is not required**.

Single requirement config example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: _include/jwt-authn-single-filter.yaml
    :language: yaml
    :lines: 32-63
    :lineno-start: 32
    :linenos:
    :caption: :download:`jwt-authn-single-filter.yaml <_include/jwt-authn-single-filter.yaml>`

Above config uses single requirement rule, each rule may have either an empty requirement or a single requirement with one provider name.

Group requirement config example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: _include/jwt-authn-group-filter.yaml
    :language: yaml
    :lines: 32-77
    :lineno-start: 32
    :linenos:
    :caption: :download:`jwt-authn-group-filter.yaml <_include/jwt-authn-group-filter.yaml>`

Above config uses more complex *group* requirements:

* The first *rule* specifies ``requires_any``; if any of ``provider1`` or ``provider2`` requirement is satisfied, the request is OK to proceed.
* The second *rule* specifies ``requires_all``; only if both ``provider1`` and ``provider2`` requirements are satisfied, the request is OK to proceed.

Copy validated JWT claims to HTTP request headers example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If a JWT is valid, you can add some of its claims of type (string, integer, boolean) to a new HTTP header to pass to the upstream. You can specify claims and headers in
`claim_to_headers` field. Nested claims are also supported.

The field :ref:`claim_to_headers <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.claim_to_headers>` is a repeat of message :ref:`JWTClaimToHeader <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JWTClaimToHeader>` which has two fields:

* Field ``header_name`` specifies the name of new http header reserved for jwt claim. If this header is already present with some other value then it will be replaced with the claim value. If the claim value doesn't exist then this header wouldn't be available for any other value.
* Field ``claim_name`` specifies the claim from verified jwt token.

.. literalinclude:: _include/jwt-authn-claim-filter.yaml
    :language: yaml
    :lines: 32-41
    :lineno-start: 32
    :linenos:
    :caption: :download:`jwt-authn-claim-filter.yaml <_include/jwt-authn-claim-filter.yaml>`

In this example the `tenants` claim is an object, therefore the JWT claim ("sub", "nested.claim.key" and "tenants") will be added to HTTP headers as following format:

.. code-block::

    x-jwt-claim-sub: <JWT Claim>
    x-jwt-claim-nested-key: <JWT Claim>
    x-jwt-tenants: <Base64 encoded JSON JWT Claim>
