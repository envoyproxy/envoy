.. _arch_overview_google_vrp:

Google 漏洞奖励计划（VRP）
=========================================

Envoy 是 `谷歌的漏洞奖励计划（VRP） <https://www.google.com/about/appsecurity/reward-program/>`_ 的参与者。
该奖励计划开放给所有的安全研究人员，并会根据下面的规则，为漏洞发现和报告提供奖励。

.. _arch_overview_google_vrp_rules:

规则
-----

VRP 的目标是提供一个正式流程来表彰那些对 Envoy 安全有贡献的外部安全人员。符合该奖励计划的漏洞应该满足以下条件：

1. 漏洞必须满足以下 :ref:`目标 <arch_overview_google_vrp_objectives>` 之一，使用提供的基于 Docker 的
   :ref:`执行环境 <arch_overview_google_vrp_ee>` 进行演示，并且与项目中的
   :ref:`威胁模型 <arch_overview_google_vrp_threat_model>` 保持一致。

2. 漏洞必须报告给 envoy-security@googlegroups.com，并保证具有潜在的安全问题的版本不被泄漏。提交报告时请遵循
   :repo:`披露指南 <SECURITY.md#disclosures>` 。披露 SLO 被记录在 :repo:`这里 <SECURITY.md#fix-and-disclosure-slos>`。
   一般而言，安全信息的披漏要遵守 `Linux 基金会的隐私政策 <https://www.linuxfoundation.org/privacy/>`_ ，
   并附加了一个条件，即 VRP 报告（包括报告的电子邮件地址和姓名）可以基于 VRP 的目的与谷歌共享。

3. 漏洞一定不能是公共论坛上已知的，比如，Github 的问题追踪，CVE 数据库（与 Envoy 关联的）等。
   现有的 CVE 之前没有与 Envoy 的漏洞关联的，不受影响。

4. 漏洞不能同时提交给由 Google 或 `Lyft <https://www.lyft.com/security>`_ 运营的其他奖励计划。

奖励由 Envoy OSS 安全团队和 Google 决定。他们将以上述规则为条件。如果多个独立的研究人员同时报告了同一个漏洞的多个实例，
或者该漏洞已经被 OSS Envoy 的安全团队跟踪，奖金将会公平地在报告提交人中进行分配。

.. _arch_overview_google_vrp_threat_model:

威胁模型
---------

最基本的匹配 Envoy 的安全模型是 :ref:`OSS 安全态势 <arch_overview_threat_model>`。我们添加了一些临时的限制来约束程序初始化阶段的攻击。我们排除了来自以下的威胁：

* 不可信的控制平面。
* 运行时服务，比如访问日志，外部授权等。
* 不可信的上游。
* 除了以下规定的 DoS 攻击。
* 除了 HTTP 连接管理器网络过滤器和 HTTP 路由器过滤器以外的任何过滤器。
* 管理控制台，这在执行环境中是被禁用的。

我们还明确地排除了针对 Envoy 的本地攻击（比如，通过本地进程，shells 等）。所有的攻击都必须通过端口 10000 上的网络数据平面进行。
同样的，内核和 Docker 漏洞在威胁模型之外。

未来，随着程序运行环境的复杂度增加，我们可能会放宽上述这些限制。

.. _arch_overview_google_vrp_ee:

执行环境
---------------------

我们提供了 Docker 镜像作为漏洞奖励计划的运行参考环境：

* `envoyproxy/envoy-google-vrp <https://hub.docker.com/r/envoyproxy/envoy-google-vrp/tags/>`_ 镜像是基于 Envoy 进行发布的。
  只有提交漏洞时的最新版本才有资格参与 VRP。第一个可用于 VRP 的版本是 1.15.0 Envoy 版本。

* `envoyproxy/envoy-google-vrp-dev <https://hub.docker.com/r/envoyproxy/envoy-google-vrp-dev/tags/>`_
  镜像是基于 Envoy 主分支构建的，只有在提交漏洞时最后 5 天内的版本才有资格参与 VRP。在该时间点上，它们不得受到任何公开披露的漏洞的影响。

当这些镜像通过 "docker run" 启动时，有两个 Envoy 进程可用:

* Envoy 的 *edge* 监听端口是 10000 （HTTPS），通过 Envoy 的 :ref:`边缘硬化原则 <faq_edge>`，有一个配置好的 :repo:`静态配置
  </configs/google-vrp/envoy-edge.yaml>`，它具有 sinkhole，直接响应和请求转发路由规则（按顺序）

  1. `/content/*`: 路由到原始的 Envoy 服务器。
  2. `/*`: 返回 403 （拒绝）。

* *原始* Envoy 是边缘 Envoy 的上游。有一个 :repo:`静态配置 </configs/google-vrp/envoy-origin.yaml>` 只提供直接响应，
  有效地充当 HTTP 源服务器，有两种路由规则（按顺序）：

  1. `/blockedz`: 返回 200 `hidden treasure`。除非存在一个限定的漏洞，否则 Envoy 边缘服务器的 10000 端口上的通信永远不可能接收到此响应。
  2. `/*`: 返回 200 `normal`。

运行 Docker 镜像，应该提供以下命令行选项：

* `-m 3g` 确保内存被限制到 3GB， 至少应该有这么多的内存可供执行环境使用。每个 Envoy 进程都有一个配置为限制在 1GB 的过载管理器。

* `-e ENVOY_EDGE_EXTRA_ARGS="<...>"` 支持边缘 Envoy 的其他 CLI 参数。这需要设置，但是可以为空。

* `-e ENVOY_ORIGIN_EXTRA_ARGS="<...>"` 支持原始 Envoy 的其他 CLI 参数，这也需要设置，但是也可以为空。

.. _arch_overview_google_vrp_objectives:

目标
-----

漏洞将在端口为 10000 的请求中被证明，这些请求触发了属于以下类别之一的故障模式：

* 死亡查询： 导致 Envoy 进程立即出错或者终止请求
* OOM：导致边缘 Envoy 进程内存溢出的请求，连接或流总数不应该有超过 100 个，否则会导致这种情况的发生（即暴力破解，不包括连接/流 DoS）。
* 绕过路由规则： 能够访问`隐藏宝藏`的请求。
* TLS 证书泄漏：请求可能获取边缘 Envoy 的 `serverkey.pem`。
* 远程代码利用：通过网络数据平面获得的任何超级管理员 shell。
* 在 OSS Envoy 安全团队的评判后，如果足够引人关注的漏洞不属于上述类别，很可能属于高级别或关键级别的漏洞。

在 Docker 镜像下运行
---------------------

执行环境的一个基本调用将在本地端口 10000 上调出边缘 Envoy，如下所示:

.. code-block:: bash

   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="" \
     envoyproxy/envoy-google-vrp-dev:latest

在调试时，额外的参数可能会很有用，例如，为了获得跟踪日志，可以使用 `wireshark` 和 `gdb`：

.. code-block:: bash

   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="-l trace" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="-l trace" \
     --cap-add SYS_PTRACE --cap-add NET_RAW --cap-add NET_ADMIN \
     envoyproxy/envoy-google-vrp-dev:latest

你可以在 Docker 容器中获取一个 shell：

.. code-block:: bash

  docker exec -it envoy-google-vrp /bin/bash


Docker 镜像包括 gdb, strace, tshark (欢迎通过 PRs 更新
:repo:`Docker 构建文件 </ci/Dockerfile-envoy-google-vrp>` 来提供其他建议
)。

重建 Docker 镜像
-----------------

这有助于重新生成你自己的 Docker 基础镜像，来用于研究的目的。要在不依赖 CI 的情况下执行此操作，
请按照 :repo:`ci/docker_rebuild_google-vrp.sh` 顶部的说明进行操作。示例如下：

.. code-block:: bash

   bazel build //source/exe:envoy-static
   ./ci/docker_rebuild_google-vrp.sh bazel-bin/source/exe/envoy-static
   docker run -m 3g -p 10000:10000 --name envoy-google-vrp \
     -e ENVOY_EDGE_EXTRA_ARGS="" \
     -e ENVOY_ORIGIN_EXTRA_ARGS="" \
     envoy-google-vrp:local
