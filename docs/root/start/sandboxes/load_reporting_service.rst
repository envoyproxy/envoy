.. _install_sandboxes_load_reporting_service:

负载报告服务 (LRS)
=================================

这个简单的例子展示了负载报告服务 (LRS) 的特性以及如何使用它。

假设下游集群 A 和上游集群 B 以及上游集群 C 进行交互。当我们打开了集群 A 的负载报告功能，LRS 服务器即会向集群 A 发送 LoadStatsResponse 并且向集群 B 和 集群 C 发送 LoadStatsResponse.Clusters。
接着，LRS 服务器随后就将收到来自集群 A、集群 B 和集群 C 的 LoadStatsRequests （包括所有请求，成功请求等）。

在这个例子中，所有的入向请求都被 Envoy 路由到一个简单的 golang web server，我们称之为 http_server。
我们为这个 http_server 初始化了两个容器实例，这两个实例会随机地向对方发送请求。并配置 Envoy 建立到 LRS 服务器的连接。
LRS 服务器通过发送 LoadStatsResponse 来使能状态统计。所有发向 http_server 的成功请求会被计数并且显示在 LRS 服务器的log中。


运行沙盒
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3: 构建沙盒
*************************

终端 1 ::

    $ pwd
    envoy/examples/load_reporting_service
    $ docker-compose pull
    $ docker-compose up --scale http_service=2


终端 2 ::

    $ pwd
    envoy/examples/load_reporting_service
    $ docker-compose ps

                                Name                               Command               State                           Ports
    --------------------------------------------------------------------------------------------------------------------------------------
    load-reporting-service_http_service_1   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 0.0.0.0:80->80/tcp, 0.0.0.0:8081->8081/tcp
    load-reporting-service_http_service_2   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 0.0.0.0:81->80/tcp, 0.0.0.0:8082->8081/tcp
    load-reporting-service_lrs_server_1     go run main.go                   Up      0.0.0.0:18000->18000/tcp

步骤 4: 开始发送 HTTP 请求流
*********************************************

终端 2 ::

  $ pwd
  envoy/examples/load_reporting_service
  $ bash send_requests.sh

上面的脚本（``send_requests.sh``）会随机地向每个 Envoy 发送请求，随后这些请求会被转发到后端服务。

步骤 5: 查看 Envoy 统计数据
**********************************************

你会看到如下结果

终端 1 ::

    ............................
    lrs_server_1    | 2020/02/12 17:08:20 LRS Server is up and running on :18000
    lrs_server_1    | 2020/02/12 17:08:23 Adding new cluster to cache `http_service` with node `0022a319e1e2`
    lrs_server_1    | 2020/02/12 17:08:24 Adding new node `2417806c9d9a` to existing cluster `http_service`
    lrs_server_1    | 2020/02/12 17:08:25 Creating LRS response for cluster http_service, node 2417806c9d9a with frequency 2 secs
    lrs_server_1    | 2020/02/12 17:08:25 Creating LRS response for cluster http_service, node 0022a319e1e2 with frequency 2 secs
    http_service_2  | 127.0.0.1 - - [12/Feb/2020 17:09:06] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [12/Feb/2020 17:09:06] "GET /service HTTP/1.1" 200 -
    ............................
    lrs_server_1    | 2020/02/12 17:09:07 Got stats from cluster `http_service` node `0022a319e1e2` - cluster_name:"local_service" upstream_locality_stats:<locality:<> total_successful_requests:21 total_issued_requests:21 > load_report_interval:<seconds:1 nanos:998411000 >
    lrs_server_1    | 2020/02/12 17:09:07 Got stats from cluster `http_service` node `2417806c9d9a` - cluster_name:"local_service" upstream_locality_stats:<locality:<> total_successful_requests:17 total_issued_requests:17 > load_report_interval:<seconds:1 nanos:994529000 >
    http_service_2  | 127.0.0.1 - - [12/Feb/2020 17:09:07] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [12/Feb/2020 17:09:07] "GET /service HTTP/1.1" 200 -
    ............................
    lrs_server_1    | 2020/02/12 17:09:09 Got stats from cluster `http_service` node `0022a319e1e2` - cluster_name:"local_service" upstream_locality_stats:<locality:<> total_successful_requests:3 total_issued_requests:3 > load_report_interval:<seconds:2 nanos:2458000 >
    lrs_server_1    | 2020/02/12 17:09:09 Got stats from cluster `http_service` node `2417806c9d9a` - cluster_name:"local_service" upstream_locality_stats:<locality:<> total_successful_requests:9 total_issued_requests:9 > load_report_interval:<seconds:2 nanos:6487000 >
