.. _operations_hot_restarter:

热重启 Python 封装器
==========================

通常情况下，在配置修改或代码变更后，Envoy 将会 :ref:`热重启 <arch_overview_hot_restart>` 。然而在许多情况下用户希望通过标准的流程管理器进行管理（例如 monit、runit）。所以我们提供 :repo:`/restarter/hot-restarter.py` 来简化这个流程。

重启的调用方式如下：

.. code-block:: console

  hot-restarter.py start_envoy.sh

`start_envoy.sh` 可以按如下方法定义（类似 salt/jinja 语法）：

.. code-block:: jinja

  #!/bin/bash

  ulimit -n {{ pillar.get('envoy_max_open_files', '102400') }}
  sysctl fs.inotify.max_user_watches={{ pillar.get('envoy_max_inotify_watches', '524288') }}

  exec /usr/sbin/envoy -c /etc/envoy/envoy.cfg --restart-epoch $RESTART_EPOCH --service-cluster {{ grains['cluster_name'] }} --service-node {{ grains['service_node'] }} --service-zone {{ grains.get('ec2_availability-zone', 'unknown') }}

留意 `inotify.max_user_watches`：当 Envoy 被配置用于监视 Linux 机器目录中的多个文件时，请增加此值，因为 Linux 强制限制了最大可监视的文件数。

*RESTART_EPOCH* 环境变量由重启程序在每次重启的时候设置，并且必须通过 :option:`--restart-epoch` 选项完成传递。

.. warning::

   如果使用 :option:`--use-dynamic-base-id` 选项要特别小心。只有当 *RESTART_EPOCH* 是 0，
   并且你的 *start_envoy.sh* 必须获得所选的基本 ID（例如 :option:`--base-id-path` ）并存储，
   然后在后续的调用中作为 :option:`--base-id` 的值（当 *RESTART_EPOCH* 大于 0 时），才能设置该标志。


重启程序处理以下信号：

* **SIGTERM** 或 **SIGINT** （Ctrl-C）：快速地清除所有子进程并退出。
* **SIGHUP**：通过重新调用作为热重启脚本的第一个参数来进行热重启。
* **SIGCHLD**：如果任意子进程意外关闭，热重启脚本将会关闭所有进程并退出，以避免处于意外状态。然后，控制进程管理器应该再次启动重启脚本来启动 Envoy 。
* **SIGUSR1**：作为重新打开所有访问日志的型号发送给 Envoy，这是用于原子移动和重新打开日志循环。
