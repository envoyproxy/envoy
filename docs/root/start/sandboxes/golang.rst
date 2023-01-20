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

See here :repo:`filter.go <contrib/golang/filters/http/source/go/pkg/api/filter.go>` for an overview of Envoy's golang filter APIs.

:ref:`See here <config_http_filters_golang>` for an overview of Envoy's Golang filter and documentation.

Step 1: Compile the go plugin library
*************************************

Build the go plugin library.

.. code-block:: console

   $ docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

The compiled library should now be in the ``lib`` folder.

.. code-block:: console

   $ ls -l lib
   total 5256
   -r-xr-xr-x 1 root root 5305111 Jan 10 16:49 filter.so

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

Step 3: Test golang plugin for header
*************************************

The output from the ``curl`` command below should include the header added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8080 2>&1 | grep rsp-header-from-go
   < rsp-header-from-go: bar-test                      <-- This is added by the common golang filter. --<

Step 4: Test golang plugin for body
***********************************

The output from the ``curl`` command below should include the body that added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/forbidden 2>&1 | grep "forbidden"
   < forbidden from go, path: /forbidden               <-- This is added by the golang filter. --<

Step 5: Test golang plugin for response status
**********************************************

The output from the ``curl`` command below should include the 403 status that added by the golang filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8080/forbidden  2>&1 | grep "403"
   <  HTTP/1.1 403 Forbidden                           <-- This is added by the common golang filter. --<

Step 6: Test golang plugin for update body
******************************************

The output from the ``curl`` command below should include the body that modiffyed by the golang filter.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/localreply/forbidden  2>&1 | grep "forbidden"
   < localreply forbidden by encodedata                <-- This is modifyed by the golang filter. --<
