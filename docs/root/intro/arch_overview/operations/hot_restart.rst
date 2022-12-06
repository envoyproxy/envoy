.. _arch_overview_hot_restart:

Hot restart
===========

Ease of operation is one of the primary goals of Envoy. In addition to robust statistics and a local
administration interface, Envoy has the ability to “hot” or “live” restart itself. This means that
Envoy can fully reload itself (both code and configuration) without dropping existing connections
during the :ref:`drain process <arch_overview_draining>`. The hot restart functionality has the
following general architecture:

* The two active processes communicate with each other over unix domain sockets using a basic RPC
  protocol. All counters are sent from the old process to the new process over the unix domain, and
  gauges are transported except those marked with ``NeverImport``. After hot restart is finished, the
  gauges transported from the old process will be cleanup, but special gauge like
  :ref:`server.hot_restart_generation statistic <server_statistics>` is retained.
* The new process fully initializes itself (loads the configuration, does an initial service
  discovery and health checking phase, etc.) before it asks for copies of the listen sockets from
  the old process. The new process starts listening and then tells the old process to start
  draining.
* During the draining phase, the old process attempts to gracefully close existing connections. How
  this is done depends on the configured filters. The drain time is configurable via the
  :option:`--drain-time-s` option and as more time passes draining becomes more aggressive.
* After drain sequence, the new Envoy process tells the old Envoy process to shut itself down.
  This time is configurable via the :option:`--parent-shutdown-time-s` option.
* Envoy’s hot restart support was designed so that it will work correctly even if the new Envoy
  process and the old Envoy process are running inside different containers. Communication between
  the processes takes place only using unix domain sockets.
* An example restarter/parent process written in Python is included in the source distribution. This
  parent process is usable with standard process control utilities such as monit/runit/etc.

Envoy's default command line options assume that only a single set of Envoy processes is running on
a given host: an active Envoy server process and, potentially, a draining Envoy server process that
will exit as described above. The :option:`--base-id` or :option:`--use-dynamic-base-id` options
may be used to allow multiple, distinctly configured Envoys to run on the same host and hot restart
independently.

.. note::

 Currently, updating a listener's :ref:`socket_options <envoy_v3_api_field_config.listener.v3.Listener.socket_options>` during a hot restart isn't supported.
 The old process socket options will be used. If it is required to update socket options either do a full restart or perform an LDS based listener updated.

.. note::

 This feature is not supported on Windows.

Socket handling
---------------

By default, Envoy uses :ref:`reuse_port
<envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>` sockets on Linux for better
performance. This feature workers correctly during hot restart because Envoy passes each socket
to the new process by worker index. Thus, no connections are dropped in the accept queues of
the draining process.

.. attention::

  In the uncommon case in which concurrency changes during hot restart, no connections will be
  dropped if concurrency increases. However, if concurrency decreases some connections may be
  dropped in the accept queues of the old process workers.
