
.. _config_http_filters_oauth:

OAuth2
======

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.oauth2.v3.OAuth2>`

The OAuth2 filter also defines a route-level config message,
:ref:`OAuth2PerRoute <envoy_v3_api_msg_extensions.filters.http.oauth2.v3.OAuth2PerRoute>`,
which may be attached to
:ref:`Route.typed_per_filter_config <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>`.
The route-level config carries a complete :ref:`OAuth2Config <envoy_v3_api_msg_extensions.filters.http.oauth2.v3.OAuth2Config>`
and is intended to replace, not merge with, the filter-level config.

When using per-route configuration, keep the
:ref:`redirect_path_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.redirect_path_matcher>`
and :ref:`signout_path <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.signout_path>`
within the same route prefix as the protected resources. If cookie paths are customized, they
should cover that same prefix so that the callback and signout requests receive the OAuth cookies
needed to complete the flow. When multiple per-route OAuth2 configurations share the same host,
customizing :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>`
is recommended to avoid overlap between routes.

The OAuth filter's flow involves:

* An unauthenticated user arrives at myapp.com, and the oauth filter redirects them to the
  :ref:`authorization_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.authorization_endpoint>`
  for login. The :ref:`client_id <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.client_id>`
  and the :ref:`redirect_uri <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.redirect_uri>`
  are sent as query string parameters in this first redirect.
* After a successful login, the authn server should be configured to redirect the user back to the
  :ref:`redirect_uri <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.redirect_uri>`
  provided in the query string in the first step. In the below code example, we choose ``/callback`` as the configured match path.
  An "authorization grant" is included in the query string for this second redirect.
* Using this new grant and the :ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.token_secret>`,
  the filter then attempts to retrieve an access token from
  the :ref:`token_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.token_endpoint>`. The filter knows it has to do this
  instead of reinitiating another login because the incoming request has a path that matches the
  :ref:`redirect_path_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.redirect_path_matcher>` criteria.
  When :ref:`auth_type <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.auth_type>` is set to ``TLS_CLIENT_AUTH``,
  client authentication is performed with the TLS client certificate (RFC 8705) and
  ``token_secret`` is not required. The :ref:`token_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.token_endpoint>`
  must use a cluster configured with an mTLS transport socket (client certificate and key).
* Upon receiving an access token, the filter sets cookies so that subseqeuent requests can skip the full
  flow. These cookies are calculated using the
  :ref:`hmac_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.hmac_secret>`
  to assist in encoding.
* The filter calls ``continueDecoding()`` to unblock the filter chain.
* The filter sets ``IdToken`` and ``RefreshToken`` cookies if they are provided by Identity provider along with ``AccessToken``. These cookie names
  can be customized by setting :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>`.

When the authn server validates the client and returns an authorization token back to the OAuth filter,
no matter what format that token is, if
:ref:`forward_bearer_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_bearer_token>`
is set to true the filter will send over a
cookie named ``BearerToken`` to the upstream. Additionally, the ``Authorization`` header will be populated
with the same value.

The OAuth filter encodes URLs in query parameters using the
`URL encoding algorithm. <https://www.w3.org/TR/html5/forms.html#application/x-www-form-urlencoded-encoding-algorithm>`_

When receiving request redirected from the authorization service the Oauth filter decodes URLs from query parameters.
However the encoded character sequences that represent ASCII control characters or extended ASCII codepoints are not
decoded. The characters without defined meaning in URL according to `RFC 3986 <https://datatracker.ietf.org/doc/html/rfc3986>`_
are also left undecoded. Specifically the following characters are left in the encoded form:

* Control characters with values less than or equal ``0x1F``
* Space (``0x20``)
* DEL character (``0x7F``)
* Extended ASCII characters with values greater than or equal ``0x80``
* Characters without defined meaning in URL: ``"<>\^{}|``

.. note::
  By default, OAuth2 filter sets some cookies with the following names:
  ``BearerToken``, ``OauthHMAC``, and ``OauthExpires``. These cookie names can be customized by
  setting
  :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>`.

.. attention::

  The OAuth2 filter is currently under active development.

Example configuration
---------------------

The following is an example configuring the filter.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.oauth2.v3.OAuth2

  config:
    token_endpoint:
      cluster: oauth
      uri: oauth.com/token
      timeout: 3s
    authorization_endpoint: https://oauth.com/oauth/authorize/
    redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
    redirect_path_matcher:
      path:
        exact: /callback
    signout_path:
      path:
        exact: /signout
    credentials:
      client_id: foo
      token_secret:
        name: token
        sds_config:
          path: "/etc/envoy/token-secret.yaml"
      hmac_secret:
        name: hmac
        sds_config:
          path: "/etc/envoy/hmac.yaml"
    # (Optional): defaults to 'user' scope if not provided
    auth_scopes:
    - user
    - openid
    - email
    # (Optional): set resource parameter for Authorization request
    resources:
    - oauth2-resource
    - http://example.com

Below is a complete code example of how we employ the filter as one of
:ref:`HttpConnectionManager HTTP filters
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address:
          protocol: TCP
          address: 127.0.0.1
          port_value: 10000
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            http_filters:
            - name: envoy.filters.http.oauth2
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2
                config:
                  token_endpoint:
                    cluster: oauth
                    uri: oauth.com/token
                    timeout: 3s
                  authorization_endpoint: https://oauth.com/oauth/authorize/
                  redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
                  redirect_path_matcher:
                    path:
                      exact: /callback
                  signout_path:
                    path:
                      exact: /signout
                  credentials:
                    client_id: foo
                    token_secret:
                      name: token
                      sds_config:
                        path: "/etc/envoy/token-secret.yaml"
                    hmac_secret:
                      name: hmac
                      sds_config:
                        path: "/etc/envoy/hmac.yaml"
                  # (Optional): defaults to 'user' scope if not provided
                  auth_scopes:
                  - user
                  - openid
                  - email
                  # (Optional): set resource parameter for Authorization request
                  resources:
                  - oauth2-resource
                  - http://example.com
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
            tracing: {}
            codec_type: "AUTO"
            stat_prefix: ingress_http
            route_config:
              virtual_hosts:
              - name: service
                domains: ["*"]
                routes:
                - match:
                    prefix: "/"
                  route:
                    cluster: service
                    timeout: 5s

    clusters:
    - name: service
      connect_timeout: 5s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: service
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 8080
    - name: oauth
      connect_timeout: 5s
      type: LOGICAL_DNS
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: oauth
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: auth.example.com
                  port_value: 443
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
          sni: auth.example.com

Finally, the following code block illustrates sample contents inside a yaml file containing both credential secrets.
Both the :ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.token_secret>`
and the :ref:`hmac_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.hmac_secret>`
can be defined in one shared file.

.. code-block:: yaml

  static_resources:
    secrets:
    - name: token
      generic_secret:
        secret: <Your token secret here>
    - name: hmac
      generic_secret:
        secret: <Your hmac secret here>

The following example shows two independent route prefixes, ``/foo`` and ``/bar``, each with its
own OAuth2 client settings. The callback path and signout path stay under the same prefix as the
protected route, the cookie paths are scoped to that prefix, and the cookie names are customized so
that the two configurations remain independent on the same host:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  route_config:
    virtual_hosts:
    - name: service
      domains: ["*"]
      routes:
      - match:
          prefix: "/foo"
        route:
          cluster: local_service
        typed_per_filter_config:
          envoy.filters.http.oauth2:
            "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2PerRoute
            config:
              token_endpoint:
                cluster: google_oauth2
                uri: https://oauth2.googleapis.com/token
                timeout: 3s
              authorization_endpoint: https://accounts.google.com/o/oauth2/v2/auth
              redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/foo/callback"
              redirect_path_matcher:
                path:
                  exact: /foo/callback
              signout_path:
                path:
                  exact: /foo/signout
              cookie_configs:
                bearer_token_cookie_config:
                  path: "/foo"
                oauth_hmac_cookie_config:
                  path: "/foo"
                oauth_expires_cookie_config:
                  path: "/foo"
                id_token_cookie_config:
                  path: "/foo"
                refresh_token_cookie_config:
                  path: "/foo"
                oauth_nonce_cookie_config:
                  path: "/foo"
                code_verifier_cookie_config:
                  path: "/foo"
              credentials:
                client_id: foo
                cookie_names:
                  bearer_token: FooBearerToken
                  oauth_hmac: FooOauthHMAC
                  oauth_expires: FooOauthExpires
                  id_token: FooIdToken
                  refresh_token: FooRefreshToken
                  oauth_nonce: FooOauthNonce
                  code_verifier: FooCodeVerifier
                token_secret:
                  name: foo_client_secret
                  sds_config:
                    path: "/etc/foo-client-secret.yaml"
                hmac_secret:
                  name: hmac
                  sds_config:
                    path: "/etc/hmac-secret.yaml"
              auth_scopes:
              - openid
              - email
      - match:
          prefix: "/bar"
        route:
          cluster: local_service
        typed_per_filter_config:
          envoy.filters.http.oauth2:
            "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2PerRoute
            config:
              token_endpoint:
                cluster: google_oauth2
                uri: https://oauth2.googleapis.com/token
                timeout: 3s
              authorization_endpoint: https://accounts.google.com/o/oauth2/v2/auth
              redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/bar/callback"
              redirect_path_matcher:
                path:
                  exact: /bar/callback
              signout_path:
                path:
                  exact: /bar/signout
              cookie_configs:
                bearer_token_cookie_config:
                  path: "/bar"
                oauth_hmac_cookie_config:
                  path: "/bar"
                oauth_expires_cookie_config:
                  path: "/bar"
                id_token_cookie_config:
                  path: "/bar"
                refresh_token_cookie_config:
                  path: "/bar"
                oauth_nonce_cookie_config:
                  path: "/bar"
                code_verifier_cookie_config:
                  path: "/bar"
              credentials:
                client_id: bar
                cookie_names:
                  bearer_token: BarBearerToken
                  oauth_hmac: BarOauthHMAC
                  oauth_expires: BarOauthExpires
                  id_token: BarIdToken
                  refresh_token: BarRefreshToken
                  oauth_nonce: BarOauthNonce
                  code_verifier: BarCodeVerifier
                token_secret:
                  name: bar_client_secret
                  sds_config:
                    path: "/etc/bar-client-secret.yaml"
                hmac_secret:
                  name: hmac
                  sds_config:
                    path: "/etc/hmac-secret.yaml"
              auth_scopes:
              - openid
              - email


Notes
-----

When enabled, the OAuth filter does not protect against Cross-Site-Request-Forgery attacks on domains with
cached authentication (in the form of cookies).
It is recommended to pair this filter with the :ref:`CSRF Filter <envoy_v3_api_msg_extensions.filters.http.csrf.v3.CsrfPolicy>`
to prevent malicious social engineering.

The service must be served over HTTPS for this filter to work properly, as the cookies use ``;secure``. Without https, your
:ref:`authorization_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.authorization_endpoint>`
provider will likely reject the incoming request, and your access cookies will not be cached to bypass future logins.

The signout path will redirect the current user to '/', and clear all authentication cookies related to
the HMAC validation. Consequently, the OAuth filter will then restart the full OAuth flow at the root path,
sending the user to the configured auth endpoint.

:ref:`pass_through_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.pass_through_matcher>` provides
an interface for users to provide specific header matching criteria such that, when applicable, the OAuth flow is entirely skipped.
When this occurs, the ``oauth_passthrough`` metric is incremented but ``success`` is not.

:ref:`deny_redirect_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.deny_redirect_matcher>` can be used to specify requests for which
unauthorized response is returned on token expiration and will not automatically redirect to the authorization endpoint. Token refresh can be still performed
during those requests by enabling the :ref:`use_refresh_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.use_refresh_token>` flag.
This behavior can be useful for AJAX requests which cannot handle redirects correctly.

:ref:`use_refresh_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.use_refresh_token>` provides
the possibility to update access token by using a refresh token. If this flag is disabled then after access token expiration the user is redirected to the authorization endpoint to log in again.
A new access token is obtained using the refresh token without redirecting the user to log in again. This requires the refresh token to be provided by the authorization_endpoint when the user logs in.
If the attempt to get an access token by using a refresh token fails then the user is redirected to the authorization endpoint.

Generally, allowlisting is inadvisable from a security standpoint.

Statistics
----------

The OAuth2 filter outputs statistics in the ``<stat_prefix>.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  oauth_failure, Counter, Total requests that were denied.
  oauth_passthrough, Counter, Total request that matched a passthrough header.
  oauth_success, Counter, Total requests that were allowed.
  oauth_unauthorization_rq, Counter, Total unauthorized requests.
  oauth_refreshtoken_success, Counter, Total successfull requests for update access token using by refresh token
  oauth_refreshtoken_failure, Counter, Total failed requests for update access token using by refresh token
