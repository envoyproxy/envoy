.. _install_sandboxes_cors:

CORS Filter
===========

Cross-Origin Resource Sharing (CORS) is a method of enforcing client-side
access controls on resources by specifying external domains that are able to
access certain or all routes of your domain. Browsers use the presence of HTTP
headers to determine if a response from a different origin is allowed.

To help demonstrate how front-envoy can enforce CORS policies, we are
releasing a set of `docker compose <http://docs.docker.com/compose/>`_ sandboxes
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

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of both services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker``, ``docker-compose`` and
``docker-machine`` installed.

A simple way to achieve this is via the `Docker Toolbox <https://www.docker.com/products/docker-toolbox>`_.

**Step 2: Setup Docker Machines**

First, let's create a couple of new machines which will hold the containers.

Terminal 1

.. code-block:: console

  $ docker-machine create --driver virtualbox frontend
  $ eval $(docker-machine env frontend)

Terminal 2

.. code-block:: console

  $ docker-machine create --driver virtualbox backend
  $ eval $(docker-machine env backend)

**Step 3: Clone the Envoy repo and start all of our containers**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/cors/frontend
  $ docker-compose up --build -d
  $ docker-compose ps
          Name                                  Command                    State                  Ports
  -----------------------------------------------------------------------------------------------------------------------------------------
  frontend_front-envoy_1_cb51c62edc96        /usr/bin/dumb-init -- /bin ... Up      10000/tcp, 0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp
  frontend_frontend-service_1_491cf87432cd   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 80/tcp

Terminal 2

.. code-block:: console

  $ pwd
  envoy/examples/cors/backend
  $ docker-compose up --build -d
  $ docker-compose ps
          Name                                  Command                    State                  Ports
  -----------------------------------------------------------------------------------------------------------------------------------------
  backend_front-envoy_1_7f9d5039c86f       /usr/bin/dumb-init -- /bin ... Up      10000/tcp, 0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp
  backend_backend-service_1_c7752ae7192a   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 80/tcp

**Step 4: Test Envoy's CORS capabilities**

You can now open a browser to view your frontend service. To find the IP of
your frontend service run the following command in terminal 1.

.. code-block:: console

  $ docker-machine ip frontend

In terminal 2, run the same command to get the remote origin IP that the
frontend service will be making requests to.

Results of the cross-origin request will be shown on the page under *Request Results*.
Your browser's CORS enforcement logs can be found in the console.

For example:

.. code-block:: console

  Access to XMLHttpRequest at 'http://192.168.99.100:8000/cors/disabled' from origin 'http://192.168.99.101:8000'
  has been blocked by CORS policy: No 'Access-Control-Allow-Origin' header is present on the requested resource.

**Step 6: Check stats of backend via admin**

When Envoy runs, it can listen to ``admin`` requests if a port is configured. In the example
configs, the admin is bound to port ``8001``.

If you go to ``<backend_service_ip>:8001/stats`` you will be able to view
all of the Envoy stats for the backend. You should see the CORS stats for
invalid and valid origins increment as you make requests from the frontend cluster.

.. code-block:: none

  http.ingress_http.cors.origin_invalid: 0
  http.ingress_http.cors.origin_valid: 0
