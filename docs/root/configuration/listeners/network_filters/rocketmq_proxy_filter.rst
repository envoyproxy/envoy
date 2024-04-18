.. _config_network_filters_rocketmq_proxy:

RocketMQ proxy
==============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.rocketmq_proxy.v3.RocketmqProxy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.rocketmq_proxy.v3.RocketmqProxy>`

Apache RocketMQ is a distributed messaging system, which is composed of four types of roles: producer, consumer, name
server and broker server. The former two are embedded into user application in form of SDK; whilst the latter are
standalone servers.

A message in RocketMQ carries a topic as its destination and optionally one or more tags as application specific labels.

Producers are used to send messages to brokers according to their topics. Similar to many distributed systems,
producers need to know how to connect to these serving brokers. To achieve this goal, RocketMQ provides name server
clusters for producers to lookup. Namely, when producers attempts to send messages with a new topic, it first
tries to lookup the addresses(called route info) of brokers that serve the topic from name servers. Once producers
get the route info of the topic, they actively cache them in memory and renew them periodically thereafter. This
mechanism, though simple, effectively keeps service availability high without demanding availability of name server
service.

Brokers provides messaging service to end users. In addition to various messaging services, they also periodically
report health status and route info of topics currently served to name servers.

Major role of the name server is to serve querying of route info  for a topic. Additionally, it also purges route info
entries once the belonging brokers fail to report their health info for a configured period of time. This ensures
clients almost always connect to brokers that are online and ready to serve.

Consumers are used by application to pull message from brokers. They perform similar heartbeats to maintain alive
status. RocketMQ brokers support two message-fetch approaches: long-pulling and pop.

Using the first approach, consumers have to implement load-balancing algorithm. The pop approach, in the perspective of
consumers, is stateless.

Envoy RocketMQ filter proxies requests and responses between producers/consumer and brokers. Various statistical items
are collected to enhance observability.

At present, pop-based message fetching is implemented. Long-pulling will be implemented in the next pull request.

.. _config_network_filters_rocketmq_proxy_stats:

Statistics
----------

Every configured rocketmq proxy filter has statistics rooted at *rocketmq.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request, Counter, Total requests
  request_decoding_error, Counter, Total decoding error requests
  request_decoding_success, Counter, Total decoding success requests
  response, Counter, Total responses
  response_decoding_error, Counter, Total decoding error responses
  response_decoding_success, Counter, Total decoding success responses
  response_error, Counter, Total error responses
  response_success, Counter, Total success responses
  heartbeat, Counter, Total heartbeat requests
  unregister, Counter, Total unregister requests
  get_topic_route, Counter, Total getting topic route requests
  send_message_v1, Counter, Total sending message v1 requests
  send_message_v2, Counter, Total sending message v2 requests
  pop_message, Counter, Total poping message requests
  ack_message, Counter, Total acking message requests
  get_consumer_list, Counter, Total getting consumer list requests
  maintenance_failure, Counter, Total maintenance failure
  request_active, Gauge, Total active requests
  send_message_v1_active, Gauge, Total active sending message v1 requests
  send_message_v2_active, Gauge, Total active sending message v2 requests
  pop_message_active, Gauge, Total active poping message active requests
  get_topic_route_active, Gauge, Total active geting topic route requests
  send_message_pending, Gauge, Total pending sending message requests
  pop_message_pending, Gauge, Total pending poping message requests
  get_topic_route_pending, Gauge, Total pending geting topic route requests
  total_pending, Gauge, Total pending requests
  request_time_ms, Histogram, Request time in milliseconds
