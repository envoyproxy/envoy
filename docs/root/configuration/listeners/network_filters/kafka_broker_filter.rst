.. _config_network_filters_kafka_broker:

Kafka Broker 过滤器
===================

Apache Kafka broker 过滤器用于解码 `Apache Kafka <https://kafka.apache.org/>`_ 的客户端协议，该客户端协议中请求和响应的消息都可以解码。支持 `Kafka 2.4.0 <http://kafka.apache.org/24/protocol.html#protocol_api_keys>`_ 中的消息版本。该过滤器会尝试不影响客户端与 broker 之间的通信，因此无法解码的消息（由于 Kafka 客户端或 broker 运行的版本比该过滤器所支持的版本新）将按原样转发。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.kafka_broker.v3.KafkaBroker>`
* 此过滤器的名称应该被配置为 *envoy.filters.network.kafka_broker*。

.. attention::

   kafka_broker 过滤器是实验性的，目前正在开发中。功能可能会随着时间的推移而扩展，并且配置结构可能会发生变化。

.. _config_network_filters_kafka_broker_config:

配置
------

Kafka Broker 过滤器应与 TCP 代理过滤器一起使用，如下面的配置片段所示：

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 127.0.0.1 # Host that Kafka clients should connect to.
        port_value: 19092  # Port that Kafka clients should connect to.
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_broker
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.kafka_broker.v3.KafkaBroker
          stat_prefix: exampleprefix
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: localkafka
  clusters:
  - name: localkafka
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    load_assignment:
      cluster_name: some_service
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1 # Kafka broker's host
                  port_value: 9092 # Kafka broker's port.

Kafka broker 需要公布 Envoy 的监听器端口，而不是自己的。

.. code-block:: text

  # Listener value needs to be equal to cluster value in Envoy config
  # (will receive payloads from Envoy).
  listeners=PLAINTEXT://127.0.0.1:9092

  # Advertised listener value needs to be equal to Envoy's listener
  # (will make clients discovering this broker talk to it through Envoy).
  advertised.listeners=PLAINTEXT://127.0.0.1:19092

.. _config_network_filters_kafka_broker_stats:

统计
------

每个配置的 Kafka Broker 过滤器的统计信息都以 *kafka.<stat_prefix>.* 为根，每个消息类型具有多个统计信息。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  request.TYPE, Counter, 从 Kafka 客户端收到特定类型的请求的次数
  request.unknown, Counter, 接收到此过滤器无法识别的格式的请求的次数
  request.failure, Counter, 接收到格式无效的请求或发生其他处理异常的次数
  response.TYPE, Counter, 从 Kafka broker 处收到特定类型的响应的次数
  response.TYPE_duration, Histogram, 响应生成时间（以毫秒为单位）
  response.unknown, Counter, 接收到此过滤器无法识别的格式的响应的次数
  response.failure, Counter, 接收到格式无效的响应或发生其他处理异常的次数
