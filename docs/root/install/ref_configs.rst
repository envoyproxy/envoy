.. _install_ref_configs:

参考配置
=========

发行版源代码为三种主要的 Envoy 部署类型分别提供了一组示例配置模板：

* :ref:`服务到服务 <deployment_type_service_to_service>`
* :ref:`前置代理 <deployment_type_front_proxy>`
* :ref:`双重代理 <deployment_type_double_proxy>`

这套示例配置的目的是为了演示 Envoy 在复杂部署中的全部功能。所有特性并不适用于所有示例。有关完整文档内容请看 :ref:`参考配置 <config>`.

配置生成器
----------

Envoy 配置会变得相对复杂。在 Lyft 我们使用 `jinja <http://jinja.pocoo.org/>`_ 模板使配置变得更加易于创建和管理。发行版源代码包含一个配置生成器的版本，它与我们在 Lyft 中使用的配置生成器近似一致。我们也为上述的三个场景中的每一个都包含了示例配置模板。

* 生成器脚本： :repo:`configs/configgen.py`
* 服务对服务模板： :repo:`configs/envoy_service_to_service_v2.template.yaml`
* 前置代理模板： :repo:`configs/envoy_front_proxy_v2.template.yaml`
* 双重代理模板： :repo:`configs/envoy_double_proxy_v2.template.yaml`

从仓库的根目录运行以下命令生成示例配置：

.. code-block:: console

  mkdir -p generated/configs
  bazel build //configs:example_configs
  tar xvf $PWD/bazel-out/k8-fastbuild/bin/configs/example_configs.tar -C generated/configs

前面的命令会使用在 `configgen.py` 内部定义的一些变量生成三种完全扩展的配置。关于不同的扩展是如何工作的详细信息，请参考 `configgen.py` 的内部注释。

关于示例配置的一些注意事项：

* 假设一个 :ref:`端点发现服务（EDS） <arch_overview_service_discovery_types_eds>` 的实例运行在 `discovery.yourcompany.net` 上。
* 假设为 `yourcompany.net` 设置了 DNS。在配置模板中查找不同的实例。
* 为 `LightStep <https://lightstep.com/>`_ 配置了跟踪。要禁用此功能请开启 `Zipkin <https://zipkin.io>`_ 或者 `Datadog <https://datadoghq.com>`_ 跟踪，
  相应的请删除或者修改 :ref:`tracing configuration <envoy_api_file_envoy/config/trace/v2/trace.proto>`。
* 该配置演示了 :ref:`global rate limiting service <arch_overview_global_rate_limit>`。要禁用此功能请删除 :ref:`rate limit configuration <config_rate_limit_service>`。
* 为服务到服务的参考配置配置了 :ref:`路由发现服务（RDS） <config_http_conn_man_rds>`，并假设它运行在 `rds.yourcompany.net` 上。
* 为服务到服务的参考配置配置了 :ref:`集群发现服务（CDS） <config_cluster_manager_cds>`，并假设它运行在 `cds.yourcompany.net` 上。
