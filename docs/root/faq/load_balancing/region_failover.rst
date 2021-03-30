服务降级期间如何使 Envoy 故障转移到另一个区域？
===============================================

Envoy 使用 `优先级 <arch_overview_load_balancing_priority_levels>` 的概念来表达这样一种想法，即某一组端点应该优先于其他端点。

通过将首选端点设置为较低的优先级，只要该优先级足够可用，Envoy 将始终选择这些端点之一。这意味着可以通过将备用端点置于不同的优先级来表示常见的故障转移场景。有关更多信息参见
`优先级 <arch_overview_load_balancing_priority_levels>`。
