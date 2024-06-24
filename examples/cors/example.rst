.. _install_sandboxes_cors:

CORS filter
===========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

Cross-Origin Resource Sharing (CORS) is a method of enforcing client-side
access controls on resources by specifying external domains that are able to
access certain or all routes of your domain. Browsers use the presence of HTTP
headers to determine if a response from a different origin is allowed.

To help demonstrate how front-envoy can enforce CORS policies, we are
releasing a set of `docker compose <https://docs.docker.com/compose/>`_ sandboxes
that deploy a frontend and backend service on different origins, both behind
front-envoy.

The frontend service has a field to input the remote domain of your backend
service along with radio buttons to select the remote domain's CORS enforcement.
The CORS enforcement choices are:

  * Disabled: CORS is disabled on the route requested. This will result in a
    client-side CORS error since the required headers to be considered a
    valid CORS request are not present.
  * Open: CORS is enabled on the route requested but the allowed origin is set
    to ``*``. This is a very permissive policy and means that origin can request
    data from this endpoint.
  * Restricted: CORS is enabled on the route requested and the only allowed
    origin is ``envoyproxy.io``. This will result in a client-side CORS error.

Step 1: Start all of our containers
***********************************

Change to the ``examples/cors/frontend`` directory, and start the containers:

.. code-block:: console

  $ pwd
  envoy/examples/cors/frontend
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

            Name                          Command              State                    Ports
  -----------------------------------------------------------------------------------------------------------
  frontend_front-envoy_1        /docker-entrypoint.sh /bin ... Up           10000/tcp, 0.0.0.0:8000->8000/tcp
  frontend_frontend-service_1   python3 /code/service.py   ... Up (healthy)

Now, switch to the ``backend`` directory in the ``cors`` example, and start the containers:

.. code-block:: console

  $ pwd
  envoy/examples/cors/backend
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

            Name                         Command             State                            Ports
  -----------------------------------------------------------------------------------------------------------------------------------
  backend_backend-service_1   python3 /code/service.py   ... Up (healthy)
  backend_front-envoy_1       /docker-entrypoint.sh /bin ... Up             10000/tcp, 0.0.0.0:8002->8000/tcp, 0.0.0.0:8003->8001/tcp

Step 2: Test Envoy's CORS capabilities
**************************************

You can now open a browser to view your frontend service at http://localhost:8000.

Results of the cross-origin request will be shown on the page under *Request Results*.

Your browser's ``CORS`` enforcement logs can be found in the browser console.

For example:

.. code-block:: console

  Access to XMLHttpRequest at 'http://192.168.99.100:8002/cors/disabled' from origin 'http://192.168.99.101:8000'
  has been blocked by CORS policy: No 'Access-Control-Allow-Origin' header is present on the requested resource.

Step 3: Check stats of backend via admin
****************************************

When Envoy runs, it can listen to ``admin`` requests if a port is configured.

In the example configs, the backend admin is bound to port ``8003``.

If you browse to http://localhost:8003/stats you will be able to view
all of the Envoy stats for the backend. You should see the ``CORS`` stats for
invalid and valid origins increment as you make requests from the frontend cluster.

.. code-block:: none

  http.ingress_http.cors.origin_invalid: 2
  http.ingress_http.cors.origin_valid: 7

.. seealso::

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.
