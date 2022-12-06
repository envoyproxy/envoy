.. _how_to_dump_heap_profile_of_envoy:

How to dump heap profile of Envoy?
===================================

If Envoy binary that build with ``gperftools`` is used, please check `PPROF.md <https://github.com/envoyproxy/envoy/blob/main/bazel/PPROF.md>`_
for how to generate Envoy heap profiles.


For any Envoy binary that build with normal ``tcmalloc``, the :ref:`/heap_dump <operations_admin_interface_heap_dump>` endpoint
is supported to dump current heap profile of Envoy.

Use following Envoy process as a specific example:

.. code-block:: bash

    $ /path/to/envoy -c /path/to/config.yaml --concurrency 2

You can get a heap profile of Envoy by the following command:

.. code-block:: bash

    $ curl <Envoy IP>:<Envoy Admin Port>/dump_heap -o /heap/output/envoy.heap

And then you can analyze the outputed heap profile with pprof:

.. code-block:: bash

    $ pprof -http:localhost:9999 /heap/output/envoy.heap

.. note::
    If you dump the heap profile in the production environment and analyze it in the local environment, please ensure
    there is a Envoy binary in your local environment and the local's binary has same path with the production's one.
    And please ensure that the Envoy binary that used to analyze heap profile is a binary with function symbol
    (no stripped binary).

You can also get heap diff from two different heap profiles:

.. code-block:: bash

    $ pprof -http:localhost:9999 /heap/output/envoy_1.heap
    $ sleep 30
    $ pprof -http:localhost:9999 /heap/output/envoy_2.heap
    $ pprof -http:localhost:9999 -base /heap/output/envoy_1.heap /heap/output/envoy_2.heap
