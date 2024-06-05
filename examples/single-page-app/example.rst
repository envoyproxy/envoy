.. _install_sandboxes_single_page_app:

Single page React app (with OAuth)
==================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make HTTP requests.

   :ref:`envsubst <start_sandboxes_setup_envsubst>`
        Used to interpolate environment vars in templates.

   :ref:`jq <start_sandboxes_setup_jq>`
        Used to parse JSON.

   :ref:`mkpasswd <start_sandboxes_setup_mkpasswd>`
        Used to generate a ~random HMAC token.

This sandbox provides an example of building and developing a single page app with Envoy.

The sandbox covers a number of Envoy's features, including:

- :ref:`direct_response <config_network_filters_direct_response>`
- :ref:`OAuth <config_http_filters_oauth>`
- Dynamic xDS filesystem updates
- Websocket proxy
- Gzip :ref:`compression <config_http_filters_compressor>`
- TLS/SNI up/downstream connection/termination
- Path/host rewrites

The app is built with `React <react_>`__ using `Vite <vite_>`__ and demonstrates OAuth authentication using
Envoy's :ref:`OAuth filter <config_http_filters_oauth>`.

This covers a scenario where we want OAuth to both authenticate the user and provide credentials
for further API interactions.

This is enabled by setting the OAuth configuration
:ref:`forward_bearer_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_bearer_token>`
to ``true``

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 36-40
   :linenos:
   :lineno-start: 36
   :emphasize-lines: 3
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

.. warning::
   Setting
   :ref:`forward_bearer_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_bearer_token>`
   means the provided access token will be forwarded to any cluster/upstreams proxied by Envoy for this HTTP filter chain..

   If untrusted upstreams are present, care will need to be taken to remove any sensitive cookies, such as ``BearerToken``.

   This can be achieved by setting :ref:`request_headers_to_remove <envoy_v3_api_field_config.route.v3.VirtualHost.request_headers_to_remove>`
   for the affected route.

A dummy "Myhub" backend is provided with a minimal OAuth provider and API for use in the example.

Setup is provided to :ref:`build and update the app for production use <install_sandboxes_single_page_app_step_production_build>`,
as well as a :ref:`development environment <install_sandboxes_single_page_app_step_login>` with
:ref:`automatic code reloading <install_sandboxes_single_page_app_step_reload>`.

The production and development environments are exposed on ports ``10000`` and ``10001`` respectively.

The Myhub backend can easily be replaced with `Github <github_>`__ or some other OAuth-based upstream service,
and some :ref:`guidance is provided on how to do this <install_sandboxes_single_page_app_step_github_oauth>`.

.. _install_sandboxes_single_page_app_step_local:

Step 1: Create a ``.local`` directory for sandbox customizations
****************************************************************

Change to the ``examples/single-page-app`` directory, and create a directory to store sandbox customizations.

You can use ``.local`` which will be ignored by Git:

.. code-block:: console

   $ mkdir .local

Copy the ``ui/`` directory to ``.local`` and set the ``UI_PATH``. This will allow customizations without changing committed files.

.. code-block:: console

   $ cp -a ui .local
   $ export UI_PATH=./.local/ui

.. _install_sandboxes_single_page_app_step_hmac:

Step 2: Generate an HMAC secret
*******************************

Envoy's :ref:`OAuth filter <config_http_filters_oauth>` requires an HMAC secret for encoding credentials.

Copy the default sandbox secrets to the customization directory, and create the required HMAC secret.

Replace ``MY_HMAC_SECRET_SEED`` with a phrase of your choosing:

.. code-block:: console

   $ cp -a secrets .local
   $ HMAC_SECRET=$(echo "MY_HMAC_SECRET_SEED" | mkpasswd -s)
   $ export HMAC_SECRET
   $ envsubst < hmac-secret.tmpl.yml > .local/secrets/hmac-secret.yml

Export the path to the secrets folder for Docker:

.. code-block:: console

   $ export SECRETS_PATH=./.local/secrets

.. _install_sandboxes_single_page_app_step_start:

Step 3: Start the containers
****************************

First export ``UID`` to ensure files created by the containers are created with your user id.

Then bring up the Docker composition:

.. code-block:: console

    $ pwd
    envoy/examples/single-page-app
    $ export UID
    $ docker compose pull
    $ docker compose up --build -d
    $ docker compose ps
    NAME                          IMAGE                       COMMAND                                                    SERVICE   CREATED         STATUS         PORTS
    ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    single-page-app-envoy-1       single-page-app-envoy       "/docker-entrypoint.sh envoy -c /etc/envoy/envoy.yaml ..." envoy     2 minutes ago   Up 2 minutes           0.0.0.0:10000-10001->10000-10001/tcp, :::10000-10001->10000-10001/tcp
    single-page-app-myhub-1       single-page-app-myhub       "/opt/myhub/app.py"                                        myhub     2 minutes ago   Up 2 minutes (healthy) 0.0.0.0:7000->7000/tcp, :::7000->7000/tcp
    single-page-app-myhub-api-1   single-page-app-myhub-api   "/opt/myhub/app.py"                                        myhub-api 2 minutes ago   Up 2 minutes (healthy)
    single-page-app-ui-1          single-page-app-ui          "/entrypoint.sh dev.sh"                                    ui        2 minutes ago   Up 2 minutes (healthy)

.. _install_sandboxes_single_page_app_step_login:

Step 4: Browse to the dev app and login
***************************************

The development app should now be available at http://localhost:10001 and provide a login button:

.. image:: /start/sandboxes/_include/single-page-app/_static/spa-login.png
   :align: center

.. note::
   The dummy OAuth provider automatically trusts everyone as a hard-coded ``envoydemo`` user and redirects back to the app.

   In a real world scenario the provider would authenticate and authorize the user before proceeding.

The sandbox is configured with an inverted match on
:ref:`pass_through_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.pass_through_matcher>`.

This ignores all paths for OAuth other than:

- ``/authorize.*``
- ``/hub.*``
- ``/login``
- ``/logout``.

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 37-46
   :linenos:
   :lineno-start: 37
   :emphasize-lines: 3-8
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

When a user clicks ``login`` the app initiates the OAuth flow by calling the ``/login`` path in Envoy.

This redirects the user to the OAuth provider for authorization/authentication with a further redirect link:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 34-39
   :linenos:
   :lineno-start: 34
   :emphasize-lines: 3-4
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

On successful authorization/authentication the user is redirected back via this link to the app with the necessary OAuth
`authorization code <oauth-auth-code_>`__ to proceed:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 44-50
   :linenos:
   :lineno-start: 44
   :emphasize-lines: 3-5
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

Envoy then uses this authorization code with its client secret to confirm authorization and obtain an access token for the user:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 50-61
   :linenos:
   :lineno-start: 50
   :emphasize-lines: 3-10
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

.. literalinclude:: _include/single-page-app/secrets/myhub-token-secret.yml
   :language: yaml
   :linenos:
   :emphasize-lines: 6
   :caption: :download:`myhub-token-secret.yml <_include/single-page-app/secrets/myhub-token-secret.yml>`

Once logged in, you should be able to make queries to the API using the OAuth credentials:

.. image:: /start/sandboxes/_include/single-page-app/_static/spa-resources.png
   :align: center

.. warning::
   Envoy's OAuth implementation defaults to triggering the OAuth flow for all paths on the endpoint.

   This can readily trigger an OAuth flood as assets are requested, and doom loops when the OAuth flows fail.

   This can be avoided by restricting the paths that are used by the OAuth flow.

   The sandbox example does this by inverting the
   :ref:`pass_through_matcher <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.pass_through_matcher>`
   to only match on the required OAuth paths.

.. tip::
   The Myhub OAuth provider does not provide an expiry for issued credentials. Likewise Github may or may
   not depending on configuration. This is valid in terms of the OAuth2 specification.

   If the authorization provider does not include an expiry, Envoy will, by default, fail the authentication.

   This can be resolved by setting
   :ref:`default_expires_in <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.default_expires_in>`:

   .. literalinclude:: _include/single-page-app/envoy.yml
      :language: yaml
      :lines: 33-37
      :linenos:
      :lineno-start: 33
      :emphasize-lines: 3
      :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

.. _install_sandboxes_single_page_app_step_api:

Step 5: Make API queries
************************

For the sandbox app,
:ref:`forward_bearer_token <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.forward_bearer_token>`
is set, and so Envoy also passes the acquired access token back to the user as a cookie:

.. image:: /start/sandboxes/_include/single-page-app/_static/spa-cookies.png
   :align: center

This cookie is then passed through Envoy in any subsequent requests to the proxied Myhub API:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 76-88
   :linenos:
   :lineno-start: 76
   :emphasize-lines: 3-11
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

.. _install_sandboxes_single_page_app_step_reload:

Step 6: Live reload code changes
********************************

With your browser open on http://localhost:10001 make some change to the UI.

For example, you might change the page title:

.. code-block:: console

   $ sed -i s/Envoy\ single\ page\ app\ example/DEV\ APP/g .local/ui/index.html

The page should automatically refresh.

Likewise any changes to the Typescript app components in ``.local/ui/src/...`` should automatically reload in
the browser.

This is enabled in Envoy by allowing the proxied connection to the `Vite <vite_>`__
development backend to be "upgraded" to use Websockets:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 22-27
   :linenos:
   :lineno-start: 22
   :emphasize-lines: 3-4
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

You can view the logs for the development server with:

.. code-block:: console

   $ docker compose logs ui
   single-page-app-ui-1  | Starting (dev.sh) with user: 1000 worker /home/worker
   single-page-app-ui-1  | yarn run v1.22.19
   single-page-app-ui-1  | $ vite --host 0.0.0.0 --port 3000
   single-page-app-ui-1  |
   single-page-app-ui-1  |   VITE v5.0.10  ready in 119 ms
   single-page-app-ui-1  |
   single-page-app-ui-1  |   ➜  Local:   http://localhost:3000/
   single-page-app-ui-1  |   ➜  Network: http://172.30.0.5:3000/

You can also use ``docker attach`` should you want to interact with the process.

.. tip::
   You can manage the Typescript package using `Yarn <yarn_>`__:

   .. code-block:: console

      $ docker compose run --rm ui yarn

.. _install_sandboxes_single_page_app_step_logout:

Step 7: Log out of the app
**************************

On signing out, the app makes a request to Envoy's configured
:ref:`signout_path <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.signout_path>`:

.. literalinclude:: _include/single-page-app/envoy.yml
   :language: yaml
   :lines: 47-53
   :linenos:
   :lineno-start: 47
   :emphasize-lines: 3-5
   :caption: :download:`envoy.yml <_include/single-page-app/envoy.yml>`

This clears the cookies and the credentials stored by Envoy before returning the user to the app home page.

The app also clears any stored data associated with the user session:

.. _install_sandboxes_single_page_app_step_production_build:

Step 8: Build production assets
*******************************

First, create and set a custom ``xds/`` directory.

You will need to rebuild Envoy to ensure it sees the correct directory:

.. code-block:: console

   $ mkdir .local/production
   $ cp -a xds .local/production/
   $ export XDS_PATH=./.local/production/xds
   $ docker compose up --build -d envoy

You can build the production assets for the app with the following:

.. code-block:: console

   $ docker compose run --rm ui build.sh

After building the `React <react_>`__ app, the sandbox script automatically updates Envoy's configuration with the
static routes required to serve the app.

You can view the generated routes:

.. code-block:: console

   $ jq '.resources[0].filter_chains[0].filters[0].typed_config.route_config.virtual_hosts[0].routes' < .local/production/xds/lds.yml

.. code-block:: json

   [
     {
       "match": {
         "path": "/assets/index-dKz4clFg.js"
       },
       "direct_response": {
         "status": 200,
         "body": {
           "filename": "/var/www/html/assets/index-dKz4clFg.js"
         }
       },
       "response_headers_to_add": [
         {
           "header": {
             "key": "Content-Type",
             "value": "text/javascript"
           }
         }
       ]
     },
     {
       "match": {
         "path": "/myhub.svg"
       },
       "direct_response": {
         "status": 200,
         "body": {
           "filename": "/var/www/html/myhub.svg"
         }
       },
       "response_headers_to_add": [
         {
           "header": {
             "key": "Content-Type",
             "value": "image/svg+xml"
           }
         }
       ]
     },
     {
       "match": {
         "prefix": "/"
       },
       "direct_response": {
         "status": 200,
         "body": {
           "filename": "/var/www/html/index.html"
         }
       },
       "response_headers_to_add": [
         {
           "header": {
             "key": "Content-Type",
             "value": "text/html"
           }
         }
       ]
     }
   ]

.. note::
   This setup configures Envoy to store the necessary files in memory.

   This may be a good fit for the single page app use case, but would not scale well
   for many or large files.

.. tip::
   When you make changes to the javascript/typescript files rebuilding the app creates new routes to the
   compiled assets.

   In this case Envoy will update via xDS and use the newly routed assets.

   If you make changes only to assets that do not get a new route - e.g. ``index.html`` - you
   should both rebuild the app and restart Envoy after:

   .. code-block:: console

      $ docker compose run --rm ui build.sh
      $ docker compose restart envoy

.. _install_sandboxes_single_page_app_step_production_browse:

Step 9: Browse to the production server
***************************************

You can browse to this server on https://localhost:10000

Unlike the development endpoint the production endpoint is configured with:

- TLS (self-signed)
- Gzip compression
- Statically served assets

.. _install_sandboxes_single_page_app_step_github_oauth:

Step 10: Setup Github OAuth/API access
**************************************

.. tip::
   Setup for `Github <github_>`__ is explained in this sandbox, but it should be easy to adapt these instructions for other providers.

You will need to set up either a `Github OAuth or full app <github-oauth_>`__. The latter provides
more control and is generally preferable.

This can be done either at the `user <github-user-settings_>`_ or organization levels:

.. image:: /start/sandboxes/_include/single-page-app/_static/spa-github-oauth.png
   :align: center

.. note::

   When setting up `Github OAuth <github-oauth_>`__ you will need to provide the redirect URI

   This must match the configured URI in Envoy

   For the purposes of this example set it to https://localhost:10000.

   You will need a separate OAuth app for development.

Depending on your use case, you may also want to set up any permissions required for your app.

Once you have this set up, you will need the `provided client id and secret <github-oauth-credentials_>`__.

.. _install_sandboxes_single_page_app_step_github_config:

Step 11: Update Envoy's configuration to use Github
***************************************************

Add the `Github provided client secret <github-oauth-credentials_>`__:

.. code-block:: console

   $ TOKEN_SECRET="GITHUB PROVIDED CLIENT SECRET"
   $ export TOKEN_SECRET
   $ envsubst < secrets/token-secret.tmpl.yml > .local/secrets/github-token-secret.yml

The file created will be available in the container under ``/etc/envoy/secrets``

.. tip::
   The following instructions use ``sed``, but you may wish to make the necessary replacements
   using your editor.

   For each configuration there are 2 places to update, one for the development listener and the other for production.

Create a copy of the Envoy config and tell Docker to use it:

.. code-block:: console

   $ cp -a envoy.yml .local/envoy.yml
   $ export ENVOY_CONFIG=.local/envoy.yml

For the OAuth configuration in ``.local/envoy.yml`` set the `Github provided client secret <github-oauth-credentials_>`__:

.. code-block:: console

   $ sed -i s@client_id:\ \"0123456789\"@client_id:\ \"$GITHUB_PROVIDED_CLIENT_ID\"@g .local/envoy.yml

Replace the
:ref:`authorization_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.authorization_endpoint>`
with ``https://github.com/login/oauth/authorize``:

.. code-block:: console

   $ sed -i s@authorization_endpoint:\ http://localhost:7000/authorize@authorization_endpoint:\ https://github.com/login/oauth/authorize@g .local/envoy.yml

Replace the
:ref:`token_endpoint <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Config.token_endpoint>` >
:ref:`uri <envoy_v3_api_field_config.core.v3.HttpUri.uri>`
with ``https://github.com/login/oauth/access_token``:

.. code-block:: console

   $ sed -i s@uri:\ http://myhub:7000/authenticate@uri:\ https://github.com/login/oauth/access_token@g .local/envoy.yml

Point the
:ref:`token_secret <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.token_secret>` >
:ref:`path <envoy_v3_api_field_config.core.v3.PathConfigSource.path>`
to the ``github-token-secret.yml`` created above:

.. code-block:: console

   $ sed -i s@path:\ /etc/envoy/secrets/myhub-token-secret.yml@path:\ /etc/envoy/secrets/github-token-secret.yml@g .local/envoy.yml

Replace the :ref:`host rewrites <envoy_v3_api_field_config.route.v3.WeightedCluster.ClusterWeight.host_rewrite_literal>`:

.. code-block:: console

   $ sed -i s@host_rewrite_literal:\ api.myhub@host_rewrite_literal:\ api.github.com@g .local/envoy.yml

Finally add (or replace the ``myhub*`` clusters with) the ``github`` and ``github-api`` clusters
:download:`Github configured clusters <_include/single-page-app/_github-clusters.yml>`:

.. code-block:: console

   $ cat _github-clusters.yml >> .local/envoy.yml

Step 12: Update the app configuration to use Github
***************************************************

We need to tell the app the name of the provider.

Currently providers for Myhub and `Github <github_>`__ are implemented:

.. literalinclude:: _include/single-page-app/ui/src/providers.tsx
   :language: typescript
   :lines: 7-13
   :linenos:
   :lineno-start: 7
   :caption: :download:`providers.tsx <_include/single-page-app/ui/src/providers.tsx>`

If you followed the above steps, the `Vite <vite_>`__ app environment settings are read from ``.local/ui/.env*``:

.. code-block:: console

   $ echo "VITE_APP_AUTH_PROVIDER=github" > .local/ui/.env.local

.. _install_sandboxes_single_page_app_step_github_restart:

Step 13: Rebuild the app and restart Envoy
******************************************

.. code-block:: console

   $ docker compose run --rm ui build.sh
   $ docker compose up --build -d envoy

.. tip::
   Note the use of ``up --build -d`` rather than ``restart``.

   This is necessary as we have changed ``envoy.yml`` which is loaded into the container at build time.

Browse to the production server https://localhost:10000

You can now log in and use the `Github APIs <github-api_>`__.:

.. image:: /start/sandboxes/_include/single-page-app/_static/spa-login-github.png
   :align: center

.. seealso::

   :ref:`Envoy OAuth filter <config_http_filters_oauth>`
      Configuration reference for Envoy's OAuth filter.

   :ref:`Envoy OAuth filter API <envoy_v3_api_file_envoy/extensions/filters/http/oauth2/v3/oauth.proto>`
      API reference for Envoy's OAuth filter.

   `OAuth2 specification <oauth-spec_>`__
      OAuth 2.0 is the industry-standard protocol for authorization.

   `React <react_>`__
      The library for web and native user interfaces.

   `Vite <vite_>`__
      Next Generation Frontend Tooling.

   :ref:`Envoy Gzip Compression API <envoy_v3_api_msg_extensions.compression.gzip.compressor.v3.Gzip>`
      API and configuration reference for Envoy's gzip compression.

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   `Github OAuth apps <github-oauth_>`__
      Information about setting up `Github <github_>`__ OAuth apps.

   `Github API <github-api_>`__
      References for `Github <github_>`__'s APIs.


.. _github: https://github.com/
.. _github-api: https://api.github.com/
.. _github-oauth: https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps
.. _github-oauth-credentials: https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps#2-users-are-redirected-back-to-your-site-by-github
.. _github-user-settings: https://github.com/settings/developers
.. _oauth-auth-code: https://oauth.net/2/grant-types/authorization-code/
.. _oauth-spec: https://oauth.net/2/
.. _react: https://react.dev/
.. _vite: https://vitejs.dev/
.. _yarn: https://yarnpkg.com/
