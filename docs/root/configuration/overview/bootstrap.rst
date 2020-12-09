.. _config_overview_bootstrap:

Bootstrap 配置
---------------

要使用 xDS API，须提供一份 bootstrap 配置文件。它提供了静态的服务器配置，并配置 Envoy 来访问 :ref:`动态配置（如有需要） <arch_overview_dynamic_config>` 。在命令行中可以通过选项 `-c` 来提供，例如：

.. code-block:: console

  ./envoy -c <path to config>.{json,yaml,pb,pb_text}

其中扩展反映了底层配置关系。

:ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 消息是配置的根。在 :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 消息中有一个重要的概念就是静态和动态资源之间的区别。比如 :ref:`监听器 <envoy_v3_api_msg_config.listener.v3.Listener>` 或 :ref:`集群 <envoy_v3_api_msg_config.cluster.v3.Cluster>` 之类的资源或在 :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` 中静态提供，或由配置在 :ref:`dynamic_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中的诸如 :ref:`LDS <config_listeners_lds>` 或 :ref:`CDS <config_cluster_manager_cds>` 之类的 xDS 服务提供。
