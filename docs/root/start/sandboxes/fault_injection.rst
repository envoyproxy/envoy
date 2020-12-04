.. _install_sandboxes_fault_injection:

故障注入过滤器
===============

这个简单的样例演示了 Envoy 的 :ref:`故障注入 <config_http_filters_fault_injection>` 特性，这个特性是使用 Envoy 的 :ref:`运行时支持 <config_runtime>` 来控制的。

运行沙盒
~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：启动所有容器
********************

终端 1

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                         Command               State                             Ports
  ------------------------------------------------------------------------------------------------------------------------------
  fault-injection_backend_1   gunicorn -b 0.0.0.0:80 htt       Up      0.0.0.0:8080->80/tcp
  fault-injection_envoy_1     /docker-entrypoint.sh /usr       Up      10000/tcp, 0.0.0.0:9211->9211/tcp, 0.0.0.0:9901->9901/tcp

步骤 4：开始发送连续的 HTTP 请求流
***********************************

终端 2

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker-compose exec envoy bash
  $ bash send_request.sh

上面的脚本（``send_request.sh``）向 Envoy 发送连续的 HTTP 请求，随后，请求会被转发到后端容器。在 Envoy 中配置了故障注入，但是处于关闭状态（也就是说，请求不受任何影响）。因此，你应该看到连续的 HTTP 200 返回码。

步骤 5：测试 Envoy 的中止故障注入
**********************************

使用下面的命令，在运行时启用 *中止* 故障注入。 

终端 3

.. code-block:: console

  $ docker-compose exec envoy bash
  $ bash enable_abort_fault_injection.sh

上面的脚本对于所有的请求启用了 HTTP 中止。所以，现在你可以看到所有的请求都连续返回了 HTTP 503。

禁用中止注入：

终端 3

.. code-block:: console

  $ bash disable_abort_fault_injection.sh

步骤 6：测试 Envoy 的延迟故障注入
*********************************

使用以下命令在运行时启用 *延迟* 故障注入。

终端 3

.. code-block:: console

  $ docker-compose exec envoy bash
  $ bash enable_delay_fault_injection.sh

上面的脚本将会对 50% 的 HTTP 请求添加一个 3s 的延时。现在，你可以看到，对于所有的请求都连续的返回了 HTTP 200，但是一半请求都是延迟 3s 来完成的。

禁用延迟故障注入：

终端 3

.. code-block:: console

  $ bash disable_delay_fault_injection.sh

步骤 7：检查当前的运行时文件系统
**********************************

可以看到当前运行时文件系统的整体情况：

终端 3

.. code-block:: console

  $ tree /srv/runtime
