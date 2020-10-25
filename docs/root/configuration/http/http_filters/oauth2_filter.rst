
.. _config_http_filters_oauth:

OAuth2
======

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.oauth2.v3alpha.OAuth2>`
* This filter should be configured with the name *envoy.filters.http.oauth2*.

The OAuth filter's flow involves:

* An unauthenticated user arrives at myapp.com, and the oauth filter redirects them to the authorization_endpoint for login. The client ID is sent in the query string in this first redirect.
* After a successful login, the authn server should be configured to redirect the user back to myapp.com/callback, assuming you have chosen /callback as your match path inside the Envoy config. An "authorization grant" is included in the query string for this second redirect.
* Using this new grant and the token_secret, the filter then attempts to retrieve an access token from the token_endpoint. The filter knows to do this instead of reinitiating another login because the incoming request has a path that matches the redirect_path_matcher criteria.
* Upon receiving an access token, the filter sets cookies so that subseqeuent requests can skip the full flow. These cookies are calculated using the hmac_secret to assist in encoding.
* The filter calls continueDecoding().

When the authn server validates the client and returns an authorization token back to the OAuth filter,
no matter what format that token is, if `forward_bearer_token` is set to true the filter will send over a 
cookie named `BearerToken` to the upstream. Additionally, the `Authorization` header will be populated
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

And the below code block is an example of how we employ it as one of
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
          - name: envoy.router
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
  - name: auth
    connect_timeout: 5s
    type: LOGICAL_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: auth
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: auth.example.com
                port_value: 443
    tls_context:
      sni: auth.example.com

Notes
-----

When enabled, the OAuth filter does not protect against Cross-Site-Request-Forgery attacks on domains with
cached authentication (in the form of cookies.)
It is recommended to pair the OAuth filter with the CSRF filter to prevent malicious social engineering.

The service must be served over HTTPS for this filter to work properly, as the cookies use `;secure`.

The signout path will redirect the current user to '/', and clear all authentication cookies related to
the HMAC validation. Consequently, the OAuth filter will then restart the full OAuth flow at the root path,
sending the user to the configured auth endpoint.

Statistics
----------

The OAuth2 filter outputs statistics in the *<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  oauth_failure, Counter, Total requests that were denied.
  oauth_success, Counter, Total requests that were allowed.
  oauth_unauthorization_rq, Counter, Total unauthorized requests.
