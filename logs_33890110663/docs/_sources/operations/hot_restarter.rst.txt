.. _operations_hot_restarter:

Hot restart Python wrapper
==========================

Typically, Envoy will be :ref:`hot restarted <arch_overview_hot_restart>` for config changes and
binary updates. However, in many cases, users will wish to use a standard process manager such as
monit, runit, etc. We provide :repo:`/restarter/hot-restarter.py` to make this straightforward.

The restarter is invoked like so:

.. code-block:: console

  hot-restarter.py start_envoy.sh

``start_envoy.sh`` might be defined like so (using salt/jinja like syntax):

.. code-block:: jinja

  #!/bin/bash

  ulimit -n {{ pillar.get('envoy_max_open_files', '102400') }}
  sysctl fs.inotify.max_user_watches={{ pillar.get('envoy_max_inotify_watches', '524288') }}

  exec /usr/sbin/envoy -c /etc/envoy/envoy.cfg --restart-epoch $RESTART_EPOCH --service-cluster {{ grains['cluster_name'] }} --service-node {{ grains['service_node'] }} --service-zone {{ grains.get('ec2_availability-zone', 'unknown') }}

Note on ``inotify.max_user_watches``: If Envoy is being configured to watch many files for configuration in a directory
on a Linux machine, increase this value as Linux enforces limits on the maximum number of files that can be watched.

The *RESTART_EPOCH* environment variable is set by the restarter on each restart and must be passed
to the :option:`--restart-epoch` option.

.. warning::

   Special care must be taken if you wish to use the :option:`--use-dynamic-base-id` option. That
   flag may only be set when the *RESTART_EPOCH* is 0 and your *start_envoy.sh* must obtain the
   chosen base ID (via :option:`--base-id-path`), store it, and use it as the :option:`--base-id`
   value on subsequent invocations (when *RESTART_EPOCH* is greater than 0).

The restarter handles the following signals:

* **SIGTERM** or **SIGINT** (Ctrl-C): Will cleanly terminate all child processes and exit.
* **SIGHUP**: Will hot restart by re-invoking whatever is passed as the first argument to the
  hot restart script.
* **SIGCHLD**: If any of the child processes shut down unexpectedly, the restart script will shut
  everything down and exit to avoid being in an unexpected state. The controlling process manager
  should then restart the restarter script to start Envoy again.
* **SIGUSR1**: Will be forwarded to Envoy as a signal to reopen all access logs. This is used for
  atomic move and reopen log rotation.
