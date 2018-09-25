.. _config_http_filters_jwt_authn:

JWT Authentication
==================

This HTTP filter can be used to verify JSON Web Token (JWT). It will verify its signature, expiration time, audiences and issuer. If the JWT verification fails, its request will be rejected. If the JWT verification success, its payload could be forwarded to the upstream for further authorization if desired. JWKS needed to verify JWT signatures can be specified in the filter config or can be fetched remotely from a JWKS server.

Configuration
-------------

This HTTP filer config has two fields, *providers* and *rules*. Field *providers* specifies how a JWT should be verified, such as where to extract the token, where to fetch the public key (JWKS) and where to output its payload. Field *rules* specifies matching rules and their requirements. It a request matches a rule, its requirement applies. The requirement specifies which JWT providers should be used.

JwtProvider
~~~~~~~~~~~

It specifies how a JWT should be verified. It has following fields:

* *issuer*: the principal that issued the JWT, usually a URL or an email address.
* *audiences*: a list of JWT audiences allowed to access. A JWT containing any of these audiences will be accepted.
  If not specified, will not check audiences in the JWT.
* *local_jwks*: fetch JWKS in local data source, either in a local file or embedded in the inline string.
* *remote_jwks*: fetch JWKS from a remote HTTP server, also specify cache duration.
* *forward*: if true, JWT will be forwarded to the upstream.
* *from_headers*: extract JWT from HTTP headers.
* *from_params*: extract JWT from query parameters.
* *forward_payload_header*: forward the JWT payload in the specified HTTP header.

The **default extract location**. If *from_headers* and *from_params* is empty,  the default location to extract JWT is from HTTP header::

  Authorization: Bearer <token>

If fails to extract a JWT from there, then check query parameter key *access_token* as this example::
  
  /path?access_token=<JWT>

In the filter config, *providers* a map, to map *provider_name* to a *JwtProvider* message. The *provider_name* has to be unique, it is referred in the *JwtRequirement* message *provider_name* field.

Important note for *remote_jwks*, a **jwks_cluster** cluster is required for Envoy to talk to a JWKS server. Due to this, `OpenID connection discovery <https://openid.net/specs/openid-connect-discovery-1_0.html>`_ is not supported since the hostname to fetch JWKS is dynamic.

Config examples:

.. code-block:: yaml

  providers:
    provider_name1:
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

* *provider_name* is **provider_name1**
* *issuer* is **https://example.com**
* *audiences*: **bookstore_android.apps.googleusercontent.com** and **bookstore_web.apps.googleusercontent.com**
* *remote_jwks*: use URL **https://example.com/.well-known/jwks.json** from cluster **example_jwks_cluster**, its cache duration is 300 seconds.
* token will be extracted from the default extract locations since *from_headers* and *from_params* are empty.
* token will not be forwarded to upstream since *forward* is not set, default is **false**
* JWT payload will not be added to the request header since *forward_payload_header* is not set.

In above example, following cluster **example_jwks_cluster** is needed.

.. code-block:: yaml

  cluster:
    name: example_jwks_cluster
    type: STRICT_DNS
    hosts:
      socket_address: 
        address: example.com
        portValue: 80


Here is another config example using inline JWKS:

.. code-block:: yaml

  providers:
    provider_name2:
      issuer: https://example2.com
      local_jwks:
        inline_string: "PUBLIC-KEY"
      from_headers:
      - name: jwt-assertion
      forward: true
      forward_payload_header: x-jwt-payload

Above example specifies:

* *provider_name* is **provider_name2**
* *issuer* is **https://example2.com**
* *audiences*: not specified, JWT *aud* field will not be checked.
* *local_jwks*: JWKS is embeded in the inline string.
* *from_headers*: token will be extracted from HTTP headers as::

     jwt-assertion: <JWT>.
  
* *forward*: token will forwarded to upstream
* JWT payload will be added to the request header as following format::

    x-jwt-payload: base64_encoded(jwt_payload_in_JSON)
  

RequirementRule
~~~~~~~~~~~~~~~

It has two fields: *match* and *requires*. The field *match* specifies how a request can be matched; e.g. by HTTP headers, or by query parameters, or by path prefixes. The field *requires* specifies the JWT requirement, e.g. which provider is required. Multiple providers may be required in such forms, such as "require_all" or "require_any". 

The field *match* uses following fields to define a match:
* one of following path_specifier: *prefix*, *path*, and *regex*.
* *headers*: specify how to match HTTP headers.
* *query_parameters*: specify how to match query parameters.

The field *requires* can be specified as any one of followings:
* *provider_name*: specifies the provider name of required JwtProvider
* *provider_and_audiences*: specifies the provider with audiences. The audiences will override the one in the JwtProvider.
* *requires_any*: a list of requirements that if any of them success, it will be success.
* *requires_all*: a list of requirements that only if all of them success, it will be success.
* *allow_missing_or_failed*: A flag for a special usage. If true, all JWTs will be verified, successfully verified JWTs will output its payload results. The request will proceeded even with some verification failures. The typical use case is: there is another HTTP filter after this filter. This JWT filter is used to do JWT verification, that filter will make decision based on the results. Istio authentication feature is implemented this way.

If a request matches multiple rules, the first matched rule will apply. The order of rules is important.

If a request doesn't match any rules, or the matched rule has empty *requires* field, JWT verification is not required.

Config samples:

.. code-block:: yaml

  providers:
    jwt_provider1:
      issuer: https://example.com
      audiences:
        audience1
      local_jwks:
        inline_string: "PUBLIC-KEY"
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
        

Above config specifies one *JwtProvider* with *provider_name* as **jwt_provider1** with an **audience1** *audience* and inline_string *local_jwks*. The config has three rules. The first rule with prefix **/health** has empty *requires* field, if a request has **/health** path prefix, it doesn't need to do any JWT verification. The second rule has path prefix **/api**, its *requires" is to use **jwt_provider1** with *audiences* override of **api_audience**. If a request has **/api** path prefix, it will use **jwt_provider1** with overrided **api_audience** to verify the JWT. The third rule has capture all prefix of **/**, it will match all requests. Its *requires" is to use **jwt_provider1** to verify JWT. The *rules* are checked on the specified order, the first matched *rule* will be used. In this config, the requests not matched with the first and the second rules will match the last one, most requests will use **jwt_provider1** to verify JWT.


.. code-block:: yaml

  providers:
    provider1:
      issuer: https://provider1.com
      local_jwks:
        inline_string: "PUBLIC-KEY"
    provider2:
      issuer: https://provider2.com
      local_jwks:
        inline_string: "PUBLIC-KEY"
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
        

Above config uses *group* requirements. The first *rule* specifies *requires_any*; if any of **provider1** or **provider2** requirement is satisfied, the request is OK to proceed. The second *rule* specifies *requires_all*; only if both **provider1** and **provider2** requirements are satisfied, the request is OK to proceed.


