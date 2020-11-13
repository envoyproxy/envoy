.. _arch_overview_hot_restart:

热重启
===========

Ease of operation is one of the primary goals of Envoy. In addition to robust statistics and a local
administration interface, Envoy has the ability to “hot” or “live” restart itself. This means that
Envoy can fully reload itself (both code and configuration) without dropping any connections. The
hot restart functionality has the following general architecture:
易操作性是 Envoy 的主要目标之一。除了强大的数据统计和本地管理界面外，Envoy 还有 “热” 或者 “现场” 重启自己的能力。
这意味着 Envoy 可以完全重新加载自己（包括代码和配置）而不丢失任何连接。热重启功能有以下通用体系结构：

* Statistics and some locks are kept in a shared memory region. This means that gauges will be
  consistent across both processes as restart is taking place.
* 统计数据和一些锁要保持在共享内存区。这意味着计量仪要在进程
* The two active processes communicate with each other over unix domain sockets using a basic RPC
  protocol.
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
