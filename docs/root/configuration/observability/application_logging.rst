.. _config_application_logs:

应用日志
==========

Envoy 及其过滤器会写应用程序日志来实现可调试性。
可以给 Envoy 配置输出应用程序日志，并且日志格式可以配置成和常见日志查看器兼容的格式。
本节介绍如何配置 Envoy 以实现与每个日志查看器的集成。

使用 GKE 记录 Stackdriver
---------------------------

`Stackdriver 记录 <https://cloud.google.com/logging/>`_ 可以读取运行在 `Google Kubernetes Engine <https://cloud.google.com/kubernetes-engine/>`_ 上的容器的日志。Envoy 应该用如下的 :ref:`命令行选项 <operations_cli>` 进行配置：

* ``--log-format '%L%m%d %T.%e %t envoy] [%t][%n]%v'``：使用 `glog <https://github.com/google/glog>`_ 格式对日志进行格式化，允许 Stackdriver 解析日志的级别和时间戳。
* ``--log-format-escaped``：记录的每个字符串都将打印在一行中。
  C-style 转移序列 （例如 ``\n``）将被转义，并避免单个字符串跨越多行。这样可以确保每个日志行都使用 glog 前缀进行结构化。
* **不** 需要设置 ``--log-path`` 选项，因为 Stackdriver 可以从 STDERR 读取日志。
* 可以设置 ``--log-level`` 选项来控制输出到 Stackdriver 的日志级别。

GKE Stackdriver 的 `参考文档 <https://cloud.google.com/run/docs/logging#container-logs>`_。
