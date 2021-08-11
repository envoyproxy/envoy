
.. _config_http_filters_oauth:

OAuth2
======

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.oauth2.v3alpha.OAuth2>`
* This filter should be configured with the name *envoy.filters.http.oauth2*.

The OAuth filter's flow involves:

* An unauthenticated user arrives at myapp.com, and the oauth filter redirects them to the
  :ref:`authorization_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.authorization_endpoint>`
  for login. The :ref:`client_id <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Credentials.client_id>`
  and the :ref:`redirect_uri <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.redirect_uri>`
  are sent as query string parameters in this first redirect.
* After a successful login, the authn server should be configured to redirect the user back to the
  :ref:`redirect_uri <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.redirect_uri>`
  provided in the query string in the first step. In the below code example, we choose /callback as the configured match path.
  An "authorization grant" is included in the query string for this second redirect.
* Using this new grant and the :ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Credentials.token_secret>`,
  the filter then attempts to retrieve an access token from
  the :ref:`token_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.token_endpoint>`. The filter knows it has to do this
  instead of reinitiating another login because the incoming request has a path that matches the
  :ref:`redirect_path_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.redirect_path_matcher>` criteria.
* Upon receiving an access token, the filter sets cookies so that subseqeuent requests can skip the full
  flow. These cookies are calculated using the
  :ref:`hmac_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Credentials.hmac_secret>`
  to assist in encoding.
* The filter calls continueDecoding() to unblock the filter chain.

When the authn server validates the client and returns an authorization token back to the OAuth filter,
no matter what format that token is, if
:ref:`forward_bearer_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.forward_bearer_token>`
is set to true the filter will send over a
cookie named ``BearerToken`` to the upstream. Additionally, the ``Authorization`` header will be populated
with the same value.

.. attention::

  The OAuth2 filter is currently under active development.

Example configuration
---------------------

The following is an example configuring the filter.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.oauth2.v3alpha.OAuth2

  config:
    token_endpoint:
      cluster: oauth
      uri: oauth.com/token
      timeout: 3s
    authorization_endpoint: https://oauth.com/oauth/authorize/
    redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
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

.. code-block:: yaml

  static_resources:
    listeners:
    - name:
      address:
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          http_filters:
          - name: envoy.filters.http.oauth2
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3alpha.OAuth2
              config:
                token_endpoint:
                  cluster: oauth
                  uri: oauth.com/token
                  timeout: 3s
                authorization_endpoint: https://oauth.com/oauth/authorize/
                redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
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
    tls_context:
      sni: auth.example.com

Finally, the following code block illustrates sample contents inside a yaml file containing both credential secrets.
Both the :ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Credentials.token_secret>`
and the :ref:`hmac_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Credentials.hmac_secret>`
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


Notes
-----

When enabled, the OAuth filter does not protect against Cross-Site-Request-Forgery attacks on domains with
cached authentication (in the form of cookies).
It is recommended to pair this filter with the :ref:`CSRF Filter <envoy_v3_api_msg_extensions.filters.http.csrf.v3.CsrfPolicy>`
to prevent malicious social engineering.

The service must be served over HTTPS for this filter to work properly, as the cookies use ``;secure``. Without https, your
:ref:`authorization_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.authorization_endpoint>`
provider will likely reject the incoming request, and your access cookies will not be cached to bypass future logins.

The signout path will redirect the current user to '/', and clear all authentication cookies related to
the HMAC validation. Consequently, the OAuth filter will then restart the full OAuth flow at the root path,
sending the user to the configured auth endpoint.

:ref:`pass_through_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.pass_through_matcher>` provides
an interface for users to provide specific header matching criteria such that, when applicable, the OAuth flow is entirely skipped.
When this occurs, the ``oauth_success`` metric is still incremented.

Generally, allowlisting is inadvisable from a security standpoint.

Statistics
----------

The OAuth2 filter outputs statistics in the *<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  oauth_failure, Counter, Total requests that were denied.
  oauth_success, Counter, Total requests that were allowed.
  oauth_unauthorization_rq, Counter, Total unauthorized requests.
