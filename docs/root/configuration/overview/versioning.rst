版本控制
----------

Envoy xDS API 遵循明确定义的 :repo:`版本控制方案 <api/API_VERSIONING.md>`。Envoy 在任何时候都支持 :ref:`多个主要版本 <api_supported_versions>` 。本节中的示例摘自 v2 xDS API。

Envoy 具有用于 xDS 传输（即用于在管理服务器和 Envoy 之间移动资源的有线协议）和用于 xDS 资源的 API 版本。这些分别称为传输 API 版本和资源 API 版本。

传输版本和资源版本可以混合使用。例如，v3 资源可以通过 v2 传输协议进行传输。此外，Envoy 可以为不同的资源类型使用混合的资源版本。例如，:ref:`v3 集群 <envoy_v3_api_msg_config.cluster.v3.Cluster>` 可以与 :ref:`v2 监听器 <envoy_api_msg_Listener>` 一起使用。

传输版本和资源版本都遵循 API 版本控制的支持和弃用 :repo:`策略 <api/API_VERSIONING.md>`。

.. note::

    Envoy 将在内部以最新的 xDS 资源版本运行，并且所有受支持的版本化资源将在提取配置时透明地升级到此最新版本。例如，通过 v2 或 v3 传输或其任何混合传送的 v2 和 v3 资源将在内部转换为 v3 资源。
