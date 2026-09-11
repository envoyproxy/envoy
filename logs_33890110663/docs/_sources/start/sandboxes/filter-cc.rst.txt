.. _install_sandboxes_filter_cc:

C++ HTTP filter (statically linked)
===================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make HTTP requests.

This sandbox demonstrates how to build a native C++ HTTP filter and statically link it
into the Envoy binary using `bzlmod <https://bazel.build/external/module>`_.

The example filter is a decoder filter which reads a ``key`` and ``val`` from its
configuration, and adds them as a header to proxied requests.

.. note::
   This example uses a custom :download:`extensions_build_config <_include/filter-cc/extensions_build_config.bzl>`.

   While this is not strictly necessary to build a custom module, it allows you to control what is
   included in the built Envoy binary.

   In this case, it excludes Wasm from the build:

  .. literalinclude:: _include/filter-cc/extensions_build_config.bzl
     :language: starlark
     :lines: 10-15
     :linenos:
     :caption: :download:`extensions_build_config <_include/filter-cc/extensions_build_config.bzl>`.

Step 1: Build the Envoy binary with the filter
**********************************************

Export ``UID`` from your host system. This will ensure that the binary created inside the
build container has the same permissions as your host user:

.. code-block:: console

   $ export UID

Change to the ``filter-cc`` directory and build the Envoy binary with the ``sample``
filter statically linked:

.. code-block:: console

    $ pwd
    examples/filter-cc
    $ docker compose -f docker-compose-build.yaml run --remove-orphans filter_build

The built binary should now be in the ``bin`` folder.

.. code-block:: console

   $ ls -l bin
   total 803408
   -r-xr-xr-x 1 user user 822683320 Oct 20 10:16 envoy

Step 2: Start all of our containers
***********************************

Start the composition - an Envoy proxy which uses the binary built in Step 1, and a
backend which echos back our request:

.. code-block:: console

    $ pwd
    examples/filter-cc
    $ docker compose up --build -d
    $ docker compose ps

    NAME                       COMMAND                  SERVICE       STATUS    PORTS
    filter-cc-proxy-1          "/usr/local/bin/envo…"   proxy         running   0.0.0.0:8000->8000/tcp
    filter-cc-web_service-1    "/bin/echo-server"       web_service   running   8080/tcp

Step 3: Check the filter has added its header
*********************************************

The ``sample`` filter is configured in :download:`envoy.yaml <_include/filter-cc/envoy.yaml>`
to add a ``via: sample-filter`` header to proxied requests:

.. literalinclude:: _include/filter-cc/envoy.yaml
   :language: yaml
   :lines: 26-31
   :lineno-start: 26
   :emphasize-lines: 2-6
   :linenos:
   :caption: :download:`envoy.yaml <_include/filter-cc/envoy.yaml>`

As the backend service echos the request it receives, the header added by the filter
should be visible in the response body:

.. code-block:: console

   $ curl -s http://localhost:8000 | grep "sample-filter"
   Via: sample-filter

Step 4: Run the integration test
********************************

The example also provides an integration test which exercises the filter inside
Envoy's HTTP integration test framework, without the need to build - or run - the
Envoy binary:

.. code-block:: console

    $ pwd
    examples/filter-cc
    $ docker compose -f docker-compose-build.yaml run --remove-orphans filter_test

.. seealso::

   :ref:`HTTP filters <arch_overview_http_filters>`
      Further information about Envoy's HTTP filters.
