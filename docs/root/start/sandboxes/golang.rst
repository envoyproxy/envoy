.. _install_sandboxes_golang:

GoLang filter
=============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

.. sidebar:: Compatibility

   The provided golang library was compiled for the ``x86_64`` architecture. If you would like to use this sandbox
   with the ``arm64`` architecture, change directory to ``examples/golang/envoy-go-filter-samples`` and run ``make build``.

In this example, we show how the `Golang <https://go.dev/>`_ filter can be used with the Envoy
proxy.

The example Envoy proxy configuration a golang http filter that could modify request/response's header or body.

See here `StreamFilter <contrib.go_plugin_api.plugin.StreamFilter>`_ for an overview of Envoy's golang filter APIs.

Step 1: Start all of our containers
***********************************

Change to the ``examples/golang`` directory.

.. code-block:: console

  $ pwd
  envoy/examples/golang
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

        Name                      Command               State                          Ports
  -------------------------------------------------------------------------------------------------------------------
  golang_proxy_1         /docker-entrypoint.sh /usr ...   Up      10000/tcp, 0.0.0.0:8080->8080/tcp,:::8080->8080/tcp
  golang_web_service_1   /bin/echo-server                 Up      8080/tcp

Step 2: Send a request to the service
*************************************

The output from the ``curl`` command below should include the header added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8080 2>&1 | grep rsp-header-from-go
   < rsp-header-from-go: bar-test                      <-- This is added by the common golang filter. --<

Step 3: Send a localreply request
*********************************

The output from the ``curl`` command below should include the body that added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/localreply  2>&1 | grep "forbidden"
   < forbidden from go, path: /localreply              <-- This is added by the golang filter. --<
