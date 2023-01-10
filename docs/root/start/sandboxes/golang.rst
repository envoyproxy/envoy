.. _install_sandboxes_golang:

GoLang filter
=============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how the `Golang <https://go.dev/>`_ filter can be used with the Envoy
proxy.

The example Envoy proxy configuration a golang http filter that could modify request/response's header or body.

See here `StreamFilter <contrib/golang/filters/http/source/go/pkg/api.StreamFilter>`_ for an overview of Envoy's golang filter APIs.

Step 1: Compile the go plugin library
*************************************

Build the go plugin library.

.. code-block:: console

   $ docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

The compiled library should now be in the ``lib`` folder.

.. code-block:: console

   $ ls -l lib
   total 5184
   -r-xr-xr-x 1 root root 5304893 Jan 10 16:49 filter.so

Step 2: Start all of our containers
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

Step 3: Send a request to the service
*************************************

The output from the ``curl`` command below should include the header added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8080 2>&1 | grep rsp-header-from-go
   < rsp-header-from-go: bar-test                      <-- This is added by the common golang filter. --<

Step 4: Send a localreply request
*********************************

The output from the ``curl`` command below should include the body that added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/localreply  2>&1 | grep "forbidden"
   < forbidden from go, path: /localreply              <-- This is added by the golang filter. --<
