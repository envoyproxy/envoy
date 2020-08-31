.. _install_sandboxes_csrf:

CSRF Filter
===========

Cross-Site Request Forgery (CSRF) is an attack that occurs when a malicious
third-party website exploits a vulnerability that allows them to submit an
undesired request on a user's behalf. To mitigate this attack this filter
checks where a request is coming from to determine if the request's origin
is the same as it's destination.

To help demonstrate how front-envoy can enforce CSRF policies, we are releasing
a `docker compose <https://docs.docker.com/compose/>`_ sandbox that
deploys a service with both a frontend and backed. This service will be started
on two different virtual machines with different origins.

The frontend has a field to input the remote domain of where you would like to
send POST requests along with radio buttons to select the remote domain's CSRF
enforcement. The CSRF enforcement choices are:

  * Disabled: CSRF is disabled on the requested route. This will result in a
    successful request since there is no CSRF enforcement.
  * Shadow Mode: CSRF is not enforced on the requested route but will record
    if the request contains a valid source origin.
  * Enabled: CSRF is enabled and will return a 403 (Forbidden) status code when
    a request is made from a different origin.
  * Ignored: CSRF is enabled but the request type is a GET. This should bypass
    the CSRF filter and return successfully.

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of both services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo**

If you have not cloned the Envoy repo, clone it with:

``git clone git@github.com:envoyproxy/envoy``

or

``git clone https://github.com/envoyproxy/envoy.git``

**Step 3: Start all of our containers**

Switch to the ``samesite`` directory in the ``csrf`` example, and start the containers:

.. code-block:: console

  $ pwd
  envoy/examples/csrf/samesite
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                        Command              State                            Ports
  ----------------------------------------------------------------------------------------------------------------------
  samesite_front-envoy_1      /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
  samesite_service_1          /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp

Now, switch to the ``crosssite`` directory in the ``csrf`` example, and start the containers:

.. code-block:: console

  $ pwd
  envoy/examples/csrf/crosssite
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                       Command                State                            Ports
  ----------------------------------------------------------------------------------------------------------------------
  crosssite_front-envoy_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 0.0.0.0:8002->8000/tcp, 0.0.0.0:8003->8001/tcp
  crosssite_service_1          /docker-entrypoint.sh /bin ... Up      10000/tcp, 8000/tcp

**Step 4: Test Envoy's CSRF capabilities**

You can now open a browser at http://localhost:8002 to view your ``crosssite`` frontend service.

Enter the IP of the ``samesite`` machine to demonstrate cross-site requests. Requests
with the enabled enforcement will fail. By default this field will be populated
with ``localhost``.

To demonstrate same-site requests open the frontend service for ``samesite`` at http://localhost:8000
and enter the IP address of the ``samesite`` machine as the destination.

Results of the cross-site request will be shown on the page under *Request Results*.
Your browser's ``CSRF`` enforcement logs can be found in the browser console and in the
network tab.

For example:

.. code-block:: console

  Failed to load resource: the server responded with a status of 403 (Forbidden)

If you change the destination to be the same as one displaying the website and
set the ``CSRF`` enforcement to enabled the request will go through successfully.

**Step 5: Check stats of backend via admin**

When Envoy runs, it can listen to ``admin`` requests if a port is configured. In
the example configs, the backend admin is bound to port ``8001``.

If you browse to http://localhost:8001/stats you will be able to view
all of the Envoy stats for the backend. You should see the CORS stats for
invalid and valid origins increment as you make requests from the frontend cluster.

.. code-block:: none

  http.ingress_http.csrf.missing_source_origin: 0
  http.ingress_http.csrf.request_invalid: 1
  http.ingress_http.csrf.request_valid: 0
