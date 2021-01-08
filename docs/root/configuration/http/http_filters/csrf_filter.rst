.. _config_http_filters_csrf:

CSRF
====

此过滤器可以用来防止基于路由或虚拟主机设置的跨站点请求伪造。简单来说，CSRF 是这样一种攻击方式：恶意的第三方利用漏洞，使得他们可以仿冒用户提交非正常的请求。

在章节 1 的 `对跨站点请求伪造的强大防御 <https://seclab.stanford.edu/websec/csrf/csrf.pdf>`_ 中有一个真实的例子：

    “例如，在 2007 年末[42]，Gmail 有一个 CSRF 漏洞。当 Gmail 用户访问恶意站点时，恶意站点能够为 Gmail 生成一个请求，
    Gmail 将其视为与受害者进行持续会话的一部分。在 2007 年 11 月，一位网站攻击者利用此 CSRF 漏洞向 David Airey 的
    Gmail 账号[1]中注入了电子邮箱过滤器。”

有很多种方法来减轻 CSRF 攻击，有一些已经在 `OWASP 预防备忘单 <https://github.com/OWASP/CheatSheetSeries/blob/5a1044e38778b42a19c6adbb4dfef7a0fb071099/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.md>`_ 罗列出来了。
此过滤器采用了一种称之为来源验证的无状态减轻攻击模式。

此种模式依赖以下两条用于确定请求是否来自同一个主机的信息：
* 导致用户代理发布请求的源（请求源）。
* 请求将要发送到的源（目的源）。

当过滤器评估一个请求时，必须确保上述信息都是存在的，且要比较它们的值。如果请求源缺失或者来源不匹配，则请求被拒绝。例外的情况是，如果请求源被视为有效且已经被添加到了策略中。因为 CSRF 攻击专门针对状态改变的请求，因此过滤器仅对具有状态改变方法（POST、PUT 等）的 HTTP 请求起作用。

  .. note::
    由于各浏览器之间的功能不尽相同，此过滤器将通过信息头部来决定请求的来源，如果这些信息不存在，它将会返回请求头部引用中主机和端口的值。


关于 CSRF 的更多信息，可参与下面页内容。

* https://www.owasp.org/index.php/Cross-Site_Request_Forgery_%28CSRF%29
* https://seclab.stanford.edu/websec/csrf/csrf.pdf
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.csrf.v3.CsrfPolicy>`

  .. note::

    此过滤器的名称应该配置为 *envoy.filters.http.csrf*。

.. _csrf-configuration:

配置
-------

CSRF 过滤器支持扩展能力，用来扩展它认为有效的请求源。它之所以能够做到这一点，且同时又能减轻了跨站点
请求伪造的企图的原因，在于 front-envoy 应用过滤器时已经达到了目的源。这就意味着虽然端点可能支持跨源请求，
但它们依旧受到保护，以避免那些不被允许的恶意的第三方的攻击。

需要注意的是，请求通常应该来自与目标相同的来源，但有些用例可能做到这一点。比如，你正在第三方供应商上托管了一个静态网站，但是又需要因追踪目的发出一些请求。

.. warning::

  其他的来源可以是精确的字符串、正则表达式、前缀字符串或者后缀字符串。当添加正则表达式、前缀或后缀来源时，
  建议谨慎行事，因为不明确的来源可能会造成安全漏洞。

.. _csrf-runtime:

运行时
--------

可以通过 :ref:`filter_enabled
<envoy_v3_api_field_extensions.filters.http.csrf.v3.CsrfPolicy.filter_enabled>` 字段中的 :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` 值来配置启用过滤器的请求占比。

可以通过 :ref:`shadow_enabled <envoy_v3_api_field_extensions.filters.http.csrf.v3.CsrfPolicy.shadow_enabled>` 字段中的
:ref:`runtime_key <envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` 值来配置仅在影子模式下启用过滤器的请求占比。仅当在影子模式下，过滤器将评估请求的 *Origin* 和 *Destination* 来决定它是否是有效的，但是不会强制执行任何策略。

.. note::

  如果同时开启了 ``filter_enabled`` 和 ``shadow_enabled``，则 ``filter_enabled`` 标记位会优先起作用。

.. _csrf-statistics:

统计
-----

CSRF 过滤器输出的统计信息在 <stat_prefix>.csrf.* 命名空间下。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  missing_source_origin, Counter, 请求源头部缺失的请求总数。
  request_invalid, Counter, 请求源和目的源不匹配的请求总数。
  request_valid, Counter, 请求源和目的源匹配的请求总数。
