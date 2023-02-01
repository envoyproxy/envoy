.. _install_sandboxes_golang:

GoLang filter
=============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how the `Golang <https://go.dev/>`_ filter can be used with the Envoy
proxy.

The example demonstrates a Go plugin that can respond directly to requests and also update responses provided by an upstream server.

 It also shows how Go plugins can be independently configured at runtime.

Step 1: Compile the go plugin library
*************************************

Build the go plugin library.

.. code-block:: console

   $ docker-compose -f docker-compose-go.yaml up --remove-orphans go_plugin_compile

The compiled library should now be in the ``lib`` folder.

.. code-block:: console

   $ ls -l lib
   total 5256
   -r-xr-xr-x 1 root root 5305111 Jan 10 16:49 simple.so

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

Step 3: Make a request handled by the Go plugin
***********************************************

The output from the ``curl`` command below should include the header added by the simple Go plugin.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8080 2>&1 | grep rsp-header-from-go
   < rsp-header-from-go: bar-test                              <-- This is added by the simple Go plugin. --<

Step 4: Make a request handled upstream and updated by the Go plugin
********************************************************************

The output from the ``curl`` command below should include the body that updated by the simple Go plugin.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/update_upstream_response 2>&1 | grep "update"
   updated upstream response body by the simple plugin               <-- This is updated by the simple Go plugin. --<

Step 5: Make a request handled by the Go plugin using custom configuration
**************************************************************************

The output from the ``curl`` command below should include the body that contains value of
``prefix_localreply_body`` by the simple Go plugin.

Terminal 1

.. code-block:: console

   $ curl localhost:8080/localreply_by_config  2>&1 | grep "localreply"
   localreply from go, path: /localreply_by_config           <-- This is response directly by the simple Go plugin. --<

.. seealso::

   :ref:`Envoy Go filter <config_http_filters_golang>`
      Further information about the Envoy Go filter.
   :ref:`Go extension API <envoy_v3_api_file_contrib/envoy/extensions/filters/http/golang/v3alpha/golang.proto>`
      The Go extension filter API.
   :repo:`Go plugin API <contrib/golang/filters/http/source/go/pkg/api/filter.go>`
      Overview of Envoy's Go plugin APIs.
