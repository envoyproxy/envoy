.. _install_sandboxes_rbac:

Role Based Access Control (RBAC) - HTTP
=======================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

RBAC is used to check if the incoming request is authorized or not.

Envoy supports 2 types for RBAC:

- L4 connections via the :ref:`Role Based Access Control (RBAC) Network Filter <config_network_filters_rbac>`
- HTTP requests via the :ref:`Role Based Access Control (RBAC) Filter <config_http_filters_rbac>`

This sandbox provides an example of RBAC of HTTP requests.

In the example, requests should only be allowed if its ``Referer`` header
matches the regex pattern ``https?://(www.)?envoyproxy.io/docs/envoy.*``.

Step 1: Start all of our containers
***********************************

Change to the ``examples/rbac`` directory and bring up the docker composition.

.. code-block:: console

  $ pwd
  envoy/examples/rbac
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

  Name             Command                          State   Ports
  ------------------------------------------------------------------------------------------------------------
  rbac_backend_1   gunicorn -b 0.0.0.0:80 htt ...   Up      0.0.0.0:8080->80/tcp
  rbac_envoy_1     /docker-entrypoint.sh /usr ...   Up      0.0.0.0:10000->10000/tcp, 0.0.0.0:10001->10001/tcp

Step 2: Denial of upstream service using RBAC
*********************************************

The sandbox is configured to proxy port ``10000`` to the upstream service.

As the request does not have the required header it is denied, and Envoy refuses the connection with an HTTP 403 return code and with the content ``RBAC: access denied``.

Now, use ``curl`` to make a request for the upstream service.

.. code-block:: console

  $ curl -si localhost:10000
  HTTP/1.1 403 Forbidden
  content-length: 19
  content-type: text/plain
  date: Thu, 28 Jul 2022 06:48:43 GMT
  server: envoy

  RBAC: access denied

Step 3: Authorization of upstream service using RBAC
****************************************************

Now, we can make another request with proper headers set.

.. code-block:: console

  $ curl -si -H "Referer: https://www.envoyproxy.io/docs/envoy" localhost:10000 | grep 200
  HTTP/1.1 200 OK

Step 4: Check stats via admin
*****************************

The sandbox is configured with the ``10001`` port for Envoy admin.

Checking the admin interface we should now see that the RBAC stats are updated, with one request denied and the other allowed

.. code-block:: console

  $ curl -s "http://localhost:10001/stats?filter=rbac"
  http.ingress_http.rbac.allowed: 1
  http.ingress_http.rbac.denied: 1
  http.ingress_http.rbac.shadow_allowed: 0
  http.ingress_http.rbac.shadow_denied: 0

.. seealso::

   :ref:`Role Based Access Control <arch_overview_rbac>`
      Learn more about using Envoy's ``RBAC`` filter.

    :ref:`RBAC Network Filter API <envoy_v3_api_msg_extensions.filters.network.rbac.v3.RBAC>`
      API and configuration reference for Envoy's ``RBAC`` network filter.

    :ref:`RBAC HTTP Filter API <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>`
      API and configuration reference for Envoy's ``RBAC`` HTTP filter.

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.
