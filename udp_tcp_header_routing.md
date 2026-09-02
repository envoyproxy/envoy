# UDP / TCP Header Routing 方案设计文档

> 目标：基于 Envoy 1.39.0，实现一个同时支持 UDP 与 TCP 的 `header_routing` 过滤器，根据 UDP 数据报 / TCP 字节流开头的自定义协议头（含房间 IP:PORT）动态转发到任意战斗房间服务器。
>
> 需求来源：`../examples/envoy_udp_routing/Client-GA-Envoy-Rooms.md`

---

## 1. 需求背景

游戏战斗服部署 500 个战斗房间，玩家根据大厅申请结果动态进入任意房间：

1. 客户端构造 UDP 数据包，包头携带房间 IP:PORT 自定义协议头，发送到 GA 加速节点；
2. GA 节点**原封不动**透传该数据包到 Envoy 代理；
3. Envoy 解析数据包开头的自定义协议头，提取目标 IP:PORT，**剥离协议头**；
4. Envoy 将剩余游戏数据动态转发到对应战斗房间服务器，响应按会话自动回传客户端（不加回头部）。

关键特性：

- 房间地址**动态变化**（容器平台按需创建/销毁房间），不能依赖预定义静态集群列表；
- 解析逻辑需同时支持 **UDP（数据报）与 TCP（字节流）** 两种协议；
- 数据面完全复用 Envoy 原生能力（会话管理、负载均衡、统计、超时）。

---

## 2. 现有代码与现状分析

`../examples/envoy_udp_routing/` 目录名虽为 UDP，但当前实现实为 **TCP 路由 WASM 示例**：

| 文件 | 内容 |
|---|---|
| `src/lib.rs` | Proxy-Wasm 插件：`StreamContext`（TCP L4），按源 IP 末位奇偶路由到 `egress-router1/2`；通过 `set_envoy_filter_state` 写 `envoy.tcp_proxy.cluster` |
| `envoy.yaml` | TCP listener :10000 + `envoy.filters.network.wasm` + `tcp_proxy` |
| `docker-compose.yaml` | Envoy v1.38 + httpbin |

可复用思想：**过滤器解析信息 → 写 filter state → 数据面按 filter state 动态选集群**。

---

## 3. 核心技术约束

**Envoy 的 Proxy-Wasm 不支持 UDP 数据面。**

1. Proxy-Wasm 扩展点仅 5 种：HTTP filter、network(L4/TCP) filter、StatsSink、AccessLogger、后台服务——**无 UDP**；
2. Proxy-Wasm ABI v0.2.1 无 UDP 回调，`proxy-wasm-rust-sdk` 仅有 `HttpContext` / `StreamContext`；
3. Envoy UDP 数据面由 `udp_proxy` 独占，其 **session filter 仅 3 个内置**：`dynamic_forward_proxy`、`ext_authz`、`http_capsule`，均不能解析自定义 payload；
4. UDP matcher 只能按源/目的 IP、端口匹配，无 payload 输入。

因此"用 WASM 解析 UDP payload"在当前 Envoy 生态不可行，必须替换实现路径（业务目标不变）。

### 3.1 方案选型

| 路径 | 做法 | 结论 |
|---|---|---|
| A. 自定义 C++ session filter + DFP | 解析头部 → 设 `envoy.upstream.dynamic_host/port` → 内置 DFP 动态转发 | **选定** |
| A'. 预定义 500 cluster | session filter 设 `envoy.udp_proxy.cluster` | 备选（房间地址固定时更简） |
| B. Dynamic Modules Rust 共享库 | 免编译 Envoy，但需自研完整 UDP 转发，绕过 udp_proxy | 否决 |
| C. 坚持 WASM | 需改 Envoy + Proxy-Wasm ABI + SDK，上游级工程 | 否决 |

选定路径 A 的理由：数据面全部复用 Envoy 原生能力；`dynamic_forward_proxy` 支持任意动态 IP:PORT，契合"动态进入任意房间"。

### 3.2 关键时序（源码级验证，Envoy 1.39.0）

三个决定性事实（`source/extensions/filters/udp/udp_proxy/udp_proxy_filter.cc`）：

1. **动态选集群**：`setClusterInfo()` 优先读 filter state `envoy.udp_proxy.cluster`（`PerSessionCluster`），否则走 matcher；
2. **DFP 读取时机**：UDP DFP 在 `onNewSession` 读 `envoy.upstream.dynamic_host/port`（空则放弃动态路由）；TCP `sni_dynamic_forward_proxy` 在 `onNewConnection` 读同 key（空则回退 SNI）；
3. **上游选择时机**：host 选择（`createUpstream`）在 session filter 链 `onNewSession` 全部走完后；filter 返回 `StopIteration` 可阻断，之后用 `continueFilterChain()`（TCP 为 `continueReading()`）续链。

由此推导出动态路由必须采用**三步模式**：

```
① 阻断：onNewSession()/onNewConnection() 返回 StopIteration，阻止立即选上游
② 首包解析：onData 中解析头部 → 剥离头部 → 设置 dynamic_host/port filter state
③ 续链：调用 continueFilterChain()/continueReading()，触发 DFP 读状态 → 选上游 → 首包转发
```

---

## 4. 总体架构

```
客户端 ──UDP/TCP 包[协议头|游戏数据]──▶ GA 加速节点 ──透明转发──▶ Envoy
                                                                  │
                          ┌──────────────────────────────────────┤
                          ▼                                      ▼
              UDP listener :10000                        TCP listener :10001
              udp_proxy + session_filters                filter_chains
              [header_routing → DFP]                     [header_routing → sni_dynamic_forward_proxy → tcp_proxy]
                          │                                      │
                    解析头/剥头/设状态                       解析头/剥头/设状态
                          ▼                                      ▼
                   动态转发到任意房间 IP:PORT（DFP cluster / DNS cache）
                          │
                          ▼
              战斗房间服务器 ──响应按会话自动回传客户端──▶ 客户端
```

- 目录与命名：`header_routing`（按机制命名，非业务命名；规避 `ext_routing` 与 `ext_authz` 混淆）
- 双协议适配器共享一个无状态 Parser（见第 6、7 章）

### 4.1 目录结构

```
envoy/
├── api/envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3/header_routing.proto   # UDP 版配置
├── api/envoy/extensions/filters/network/header_routing/v3/header_routing.proto                # TCP 版配置
├── source/common/header_routing/
│   ├── BUILD
│   ├── header_parser.h / header_parser.cc        # 共享：纯函数解析，无 filter 依赖，单测 100%
├── source/extensions/filters/udp/udp_proxy/session_filters/header_routing/
│   ├── BUILD / config.cc/.h / filter.cc/.h       # UDP 适配器（UdpSessionReadFilter）
├── source/extensions/filters/network/header_routing/
│   ├── BUILD / config.cc/.h / filter.cc/.h       # TCP 适配器（Network::ReadFilter）
```

---

## 5. 头部协议定义

```
+--------+---------+-----------+-----------+
| Magic  | Version | RoomIP    | RoomPort  |
| 1 B    | 1 B     | 4 B       | 2 B       |
+--------+---------+-----------+-----------+
```

- Magic：1 字节，防误判（默认 `0x55`，可配置）
- Version：1 字节，协议版本（默认 `1`，可配置）
- RoomIP：4 字节二进制 IPv4
- RoomPort：2 字节大端序

### 5.1 常量定义

```cpp
// 头部常量：8 字节固定头 [Magic 1B][Version 1B][RoomIP 4B][RoomPort 2B 大端]
namespace Envoy::HeaderRouting {

constexpr size_t HeaderLength = 8;   // 头部总长度（字节）
constexpr size_t MagicOffset = 0;    // Magic 字段偏移
constexpr size_t VersionOffset = 1;  // Version 字段偏移
constexpr size_t IpOffset = 2;       // RoomIP 字段偏移
constexpr size_t PortOffset = 6;     // RoomPort 字段偏移

} // namespace Envoy::HeaderRouting
```

---

## 6. 共享 Parser 接口

设计原则：**Parser 无状态纯函数 + `ParseResult` 统一 UDP/TCP 两种数据流语义**。两者唯一差异是"数据不完整时怎么办"——UDP 直接弃包，TCP 继续缓冲；该差异由枚举 `NeedMoreData` 统一表达。

### 6.1 共享配置

```cpp
// 由 UDP/TCP 两个 filter 的 proto 配置各自解析后，转成同一结构共用
struct HeaderRoutingConfig {
  uint8_t magic;    // Magic 字节，默认 0x55，防误判
  uint8_t version;  // 协议版本，默认 1
  // 是否把 8 字节协议头原封不动转发给上游（UDP/TCP proto 均新增可选字段 forward_header）：
  //  - true（默认）：Envoy 解析头部仅用于选路，之后保留头部，头部连同游戏数据一起转发给上游；
  //  - false：解析选路后剥离头部，只把游戏数据转发给上游。
  bool forward_header{true};
  // 未来头部格式变更在此扩展字段，适配器无需改动
};
```

### 6.2 结果类型

```cpp
// 解析出的目标地址：直接给"规范化字符串 IP + 端口"，适配器无需再转换
struct ParsedTarget {
  std::string ip;   // 点分十进制，如 "10.0.0.3"
  uint16_t port;    // 主机序，如 8600
};

struct ParseResult {
  enum class Status {
    Ok,           // 解析成功，target 有效
    NeedMoreData, // 数据不足头部长度
    BadMagic,     // Magic 校验失败
    BadVersion,   // Version 不支持
  };
  Status status;
  absl::optional<ParsedTarget> target; // status == Ok 时有效
};
```

### 6.3 Parser API（核心，仅一个函数）

```cpp
class HeaderParser {
public:
  // 解析输入头部。
  // 语义：data 是"可能含头部的一段字节"；
  //  - 长度不足 → NeedMoreData（UDP 视为畸形弃包，TCP 视为需继续累积）
  //  - Magic/Version 不符 → BadMagic/BadVersion
  //  - 成功 → Ok + target（IP 已转点分十进制，端口已转主机序）
  static ParseResult parse(absl::string_view data, const HeaderRoutingConfig& config);
};
```

### 6.4 边界与决策说明

| 决策 | 理由 |
|---|---|
| 无状态静态函数 | UDP 每包独立、TCP 缓冲在适配器侧持有；parser 无需实例，天然线程安全、单测简单 |
| `NeedMoreData` 统一语义 | 一次函数调用覆盖两种数据流差异，接口不泄漏数据流概念 |
| IP 返回字符串而非 4 字节 | filter state（`dynamic_host`）和 DFP 都要字符串；解析处转好，两适配器零重复 |
| 错误统计归适配器 | parser 保持纯函数，stats/drop/close 策略是各自 filter 的职责 |

---

## 7. UDP 适配器设计

- 类型：`envoy.filters.udp.session.header_routing`（`UdpSessionReadFilter`）
- 位置：`source/extensions/filters/udp/udp_proxy/session_filters/header_routing/`
- session_filters 链顺序：`[header_routing, dynamic_forward_proxy]`

### 7.1 核心逻辑

```cpp
class HeaderRoutingUdpFilter : public UdpSessionReadFilter {
public:
  // ① 阻断：阻止 udp_proxy 在 filter 链走完前选上游
  ReadFilterStatus onNewSession() override { return ReadFilterStatus::StopIteration; }

  // ② 会话级状态机（配合客户端"确认前带头、确认后停"契约，见第 13 章）：
  //    - 未确认（header_handled_ == false）：每个数据报都尝试解析头部。
  //      客户端在收到房间首响应前所有包都带头，故首包丢失时后续带头包可自愈建会话；
  //    - 已确认（header_handled_ == true）：客户端已不再带头，全部透传；
  //      且此时绝不能再调 continueFilterChain()（会重复 setClusterInfo/createUpstream）。
  ReadFilterStatus onData(Network::UdpRecvData& data) override {
    if (header_handled_) {
      return ReadFilterStatus::Continue; // 纯游戏数据，直接透传
    }
    auto result = HeaderParser::parse(data.buffer_->linearize(HeaderLength), config_);
    switch (result.status) {
    case ParseResult::Status::Ok:
      // forward_header=true（默认）保留协议头，原样转发给上游；
      // forward_header=false 时剥离协议头，仅转发游戏数据。
      if (!config_.forward_header) {
        data.buffer_->drain(HeaderLength);           // 剥离协议头
      }
      setTargetFilterState(result.target.value()); // 设 dynamic_host/port
      header_handled_ = true;                      // 只允许续链一次
      read_callbacks_->continueFilterChain();      // ③ 续链：触发 DFP.onNewSession 读状态
      return ReadFilterStatus::Continue;
    case ParseResult::Status::NeedMoreData:        // UDP 半包 = 畸形包
    case ParseResult::Status::BadMagic:
    case ParseResult::Status::BadVersion:
      dropDatagram();                              // 丢包 + 统计；客户端应重发带头包
      // 返回 StopIteration：整包已排空，终止外层 onData 循环，
      // 避免被排空的 0 字节数据报继续流向 DFP/上游（否则 writeUpstream 发出空包）。
      return ReadFilterStatus::StopIteration;
    }
  }

private:
  bool header_handled_{false}; // 会话头部是否已成功解析（每个会话一个 filter 实例）
};
```

### 7.2 filter state 写入

```cpp
// 设置 DFP 读取的目标地址（类型与 DFP 读取类型严格一致）
// Envoy 1.39 的 setData 签名：(name, data, LifeSpan, StreamSharing)，无 StateType 参数；
// 只读语义由读取方 getDataReadOnly 保证。
streamInfo().filterState()->setData(
    "envoy.upstream.dynamic_host",
    std::make_shared<Router::StringAccessorImpl>(target.ip),
    FilterState::LifeSpan::FilterChain);
streamInfo().filterState()->setData(
    "envoy.upstream.dynamic_port",
    std::make_shared<StreamInfo::UInt32AccessorImpl>(target.port),
    FilterState::LifeSpan::FilterChain);
```

### 7.3 已验证时序（两种路径均正确）

- **DNS 命中（InCache）**：`continueFilterChain` 内 DFP 同步 `createUpstream`；外层 `onData` 循环随后 `writeUpstream` 发送剥离后的首包；
- **DNS 异步（Loading）**：DFP 返回 `StopIteration` 并缓存首包；DNS 完成后 `injectDatagramToFilterChain` 续传。`onData` 统一返回 `Continue` 即可覆盖两条路径。

"确认前带头"契约下的两种容错时序（源码已验证）：

- **首包丢失**：客户端在收到房间首响应前重发的带头包，此时 `header_handled_ == false`，filter 正常解析并续链，会话照样建立，无需换源端口；
- **建会话期间的后续带头包**：首包已解析（`header_handled_ == true`）但 DFP 仍在 DNS Loading 时，后续到达的带头包被 filter 直接透传，进入 DFP 的 `buffer_options` 缓冲；DNS 完成后统一转发。头部已在首包时剥离，缓冲中的包为纯游戏数据。

---

## 8. TCP 适配器设计

- 类型：`envoy.filters.network.header_routing`（`Network::ReadFilter`）
- 位置：`source/extensions/filters/network/header_routing/`
- filter 链顺序：`[header_routing, sni_dynamic_forward_proxy, tcp_proxy]`

### 8.1 TCP 跨包分析

**结论：跨包是"可能"而非"必然"，但必须按"可能"设计（防御性缓冲）。**

- TCP 是字节流，无消息边界；头部是否跨包由 TCP 栈决定（MSS 分段、Nagle、延迟 ACK、重传）；
- 首包数据量 > MSS（约 1460B）→ **必然跨包**；小包通常一次到齐但**无协议保证**；
- 因此 `onData` 绝不能假设"头部一定齐全"，必须累积缓冲。

### 8.2 核心逻辑

```cpp
class HeaderRoutingTcpFilter : public Network::ReadFilter {
public:
  // ① 阻断：阻止 sni_dynamic_forward_proxy 在头部就绪前选上游
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::StopIteration; }

  // ② 流式解析：字节流可能跨包，先累积再判断
  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    pending_buffer_.move(data);                          // 先累积
    auto result = HeaderParser::parse(
        pending_buffer_.linearize(HeaderLength), config_);
    switch (result.status) {
    case ParseResult::Status::Ok:
      // forward_header=true（默认）保留协议头，原样转发给上游；
      // forward_header=false 时剥离协议头，仅转发游戏数据。
      if (!config_.forward_header) {
        pending_buffer_.drain(HeaderLength);               // 从累计缓冲扣头
      }
      setTargetFilterState(result.target.value());       // 设 dynamic_host/port
      read_callbacks_->continueReading();                // ③ 续链：触发 sni_dynamic_forward_proxy.onNewConnection
      return Network::FilterStatus::Continue;            // 剩余字节透传
    case ParseResult::Status::NeedMoreData:
      return Network::FilterStatus::StopIteration;       // 等待后续 segment
    case ParseResult::Status::BadMagic:
    case ParseResult::Status::BadVersion:
      closeConnection();                                 // 畸形头，关闭连接
      return Network::FilterStatus::StopIteration;
    }
  }
};
```

### 8.3 与客户端契约的对应

- TCP 是全双工可靠字节流，连接建立即"会话建立"，无 UDP 那样的"确认前/后"阶段；
- 客户端只需在连接建立后、游戏数据前**一次性发送 8 字节头**；重连 = 新连接 = 重新带头；
- 头部可能跨 segment（MSS/Nagle），已由 `pending_buffer_` 防御性累积，客户端无需处理，但建议一次拼接 `send(header + data)` 减少小包。

---

## 9. Envoy 配置要点

### 9.1 UDP listener（:10000）

```yaml
listeners:
- name: udp_listener
  address:
    socket_address: { protocol: UDP, address: 0.0.0.0, port_value: 10000 }
  udp_listener_config:
    downstream_socket_config: { max_rx_datagram_size: 4096 }
  listener_filters:
  - name: envoy.filters.udp_listener.udp_proxy
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
      stat_prefix: udp_routing
      matcher:
        on_no_match:
          action:
            name: route
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
              cluster: room_dynamic
      session_filters:
      - name: envoy.filters.udp.session.header_routing
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.header_routing.v3.HeaderRouting
          magic: 85
          version: 1
          # 可选字段，默认 false：剥离 8B 头部仅转发游戏数据；
          # true：8B 头部原封不动转发给上游（需上游协议容忍/消费头部）。
          forward_header: false
      - name: envoy.filters.udp.session.dynamic_forward_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.dynamic_forward_proxy.v3.FilterConfig
          stat_prefix: dfp
          dns_cache_config:
            name: header_routing_cache
            dns_lookup_family: V4_ONLY
```

### 9.2 TCP listener（:10001）

```yaml
- name: tcp_listener
  address:
    socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10001 }
  filter_chains:
  - filters:
    - name: envoy.filters.network.header_routing
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.header_routing.v3.HeaderRouting
        magic: 85
        version: 1
        # 可选字段，默认 true：8B 头部原封不动转发给上游（需上游协议容忍/消费头部）；
        # false：剥离 8B 头部仅转发游戏数据。
        forward_header: true
    - name: envoy.filters.network.sni_dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.sni_dynamic_forward_proxy.v3.FilterConfig
        dns_cache_config:
          name: header_routing_cache
          dns_lookup_family: V4_ONLY
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp_routing
        cluster: room_dynamic
```

### 9.3 动态转发 cluster（UDP/TCP 共用）

```yaml
clusters:
- name: room_dynamic
  connect_timeout: 5s
  # dynamic_forward_proxy 是 cluster 自带 LB 类型，必须显式声明 CLUSTER_PROVIDED，
  # 否则 cluster manager 校验报 "cluster provided LB not specified"。
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.dynamic_forward_proxy
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
      dns_cache_config:
        name: header_routing_cache
        dns_lookup_family: V4_ONLY
```

要点：UDP DFP session filter 与 cluster 的 `dns_cache_config.name` 必须一致。

---

## 10. 构建与注册

1. 新增两个 proto（UDP 版 / TCP 版），`prost`/`protoc` 生成配置代码（沿用 build.rs 机制或 Envoy 原生 proto 构建）；
2. 实现 `source/common/header_routing/`（共享 parser）；
3. 实现两个适配器（filter + config/factory）：
   - UDP：注册 `NamedUdpSessionFilterConfigFactory`（类别 `envoy.filters.udp.session`）
   - TCP：注册 `NamedNetworkFilterConfigFactory`（类别 `envoy.filters.network`）
4. 在 `extensions_build_config.bzl` / `all_extensions.bzl` 注册两个扩展名；
5. 参照 Envoy 开发规范（`AGENTS.md`）：C++ 单测 100% 覆盖、`clang-format`、`tools/local_fix_format.sh`。

---

## 11. 测试策略

| 层级 | 内容 |
|---|---|
| Parser 单测 | 正常头 / 短包（`NeedMoreData`）/ Magic 错 / Version 错 / 边界（长度恰为 8） |
| UDP filter 单测 | 首包解析 + drain + filter state + `continueFilterChain`；畸形包丢弃 + 统计；`header_handled_` 置位后透传（不再解析/剥头） |
| TCP filter 单测 | 头部跨 segment 累积（分 2~3 段喂入）；头部齐全后状态设置与续链；畸形头关连接 |
| 集成验证 | 模拟房间 UDP/TCP 服务（echo）+ 模拟 GA 客户端（带头部发包）；`/stats` 观察会话统计 |

---

## 12. 风险与注意点

| 风险 | 说明与对策 |
|---|---|
| DFP DNS 缓存过期 | 房间地址变化时缓存可能命中旧值；房间地址用 **IP 字面量**（Envoy DnsCache 对 IP 直接建 host，不经 DNS）或配置合适 TTL |
| DNS 解析期间丢包 | DFP 配 `buffer_options` 缓冲（默认 1024 数据报） |
| 500 会话并发 | udp_proxy 会话受 cluster 最大连接断路器限制（默认 1024，够用可调） |
| TCP 头部跨包 | 已按防御性缓冲设计（`pending_buffer_` + `NeedMoreData` 状态机） |
| UDP 包过大 | `max_rx_datagram_size` 需 ≥ 游戏最大数据包，否则整包被内核丢弃 |
| 头部解析安全 | 固定头 + Magic/Version 校验 + 长度校验，畸形包丢弃/关连接；**未校验目标地址合法性**——RoomIP = 0.0.0.0/255.255.255.255/组播/回环 或 RoomPort = 0 时 UDP `connect()` 本地失败（计 `sess_tx_errors_` 并丢包），可选加强：Parser 拒绝全 0/广播/组播地址 |
| UDP 首包丢失 | "确认前带头"策略：未确认期间重发均带头，Envoy `header_handled_` 未置位时可随时建会话 |
| UDP 目标不可达（行为） | Envoy 无法即时感知：UDP `connect()`/`sendmsg` 均本地成功（不握手、无反馈）；本地 `connect`/`send` 失败（如地址非法）计 `sess_tx_errors_`，ICMP Port/Host Unreachable 仅在下次读时计 `sess_rx_errors_`，**两者都不销毁会话、不通知客户端**（公网黑洞场景连计数都没有）；坏 host 在 DFP 缓存保留至 evict，期间新会话也全部发向黑洞 |
| UDP 目标不可达（对策） | 客户端以"收到房间首响应"为确认信号，超时重发带头包 N 次后**强制换源端口 + 重新向大厅申请有效房间地址**（注意：换源端口后若头部仍是同一坏 IP，新会话会命中缓存中同一坏 host，**换端口无效**）；运维监控 `cluster.<prefix>.sess_tx_errors_/sess_rx_errors_` 与 `dns_cache.<name>.*`。TCP 无此问题（`tcp_proxy` 建连失败即时感知并重试/关闭） |
| DFP host evict 强拆活跃 UDP 会话 | UDP 会话仅创建时 touch host、持续转发不 touch；host 在 refresh 检查时距上次 touch ≥ `host_ttl_`（默认 5 分钟）即被 evict，并强拆其全部活跃会话 → 客户端"确认后不再带头"的无头包在新会话被判畸形丢弃，**通信中断且不可自愈**。对策：`host_ttl` 配置 ≥ 预期最长会话时长（如 1h），让 `idle_timeout`（默认 1 分钟）先于 evict 回收空闲会话；被拆会话需客户端重新带头或换源端口恢复 |
| DFP 缓存 max_hosts 溢出 | DnsCache 默认 `max_hosts = 1024`；房间动态重建产生新 IP + 频繁换房 → 缓存 host 数触顶后新 host 加入失败（Overflow）→ 新房间路由失败。对策：按峰值房间数配置更大 `max_hosts`，并依赖 evict 清理僵尸 host |
| UDP 中途换房间 | 会话绑定四元组，换房间必须换源端口（新四元组 → 新会话 → 重新带头） |
| 确认后误带头/漏带头 | 客户端状态机与 Envoy 契约必须严格一致；漏带头 → 包被当畸形丢弃，误带头 → 8B 头透传至房间导致数据错位 |
| 可选：真实客户端 IP | `use_original_src_ip: true` 让房间侧拿到玩家真实 IP（需 `CAP_NET_ADMIN`） |

---

## 13. 客户端包头改造指南

> 服务端（Envoy）契约：本方案路由粒度是**会话**（UDP 四元组 / TCP 连接），不是单包。
> 客户端必须遵守以下头部协议，Envoy 才能正确解析与剥离。

### 13.1 通用头部格式（UDP / TCP 共用，纯 IPv4，明文）

```
+--------+---------+-----------+-----------+
| Magic  | Version | RoomIP    | RoomPort  |
| 1 B    | 1 B     | 4 B       | 2 B       |
+--------+---------+-----------+-----------+
```

- Magic：`0x55`；Version：`0x01`（两端可配置，需一致）；
- RoomIP：4 字节**网络序**（大端）IPv4；
- RoomPort：2 字节**大端**；
- 无校验字段：UDP/TCP 传输层 checksum 已覆盖头部，端到端信任 GA 节点不篡改。

### 13.2 UDP 客户端改造（确认前带头、确认后停）

```
未确认（未收到房间首个响应）                已确认（收到房间首响应）
┌─────────────────────────────┐           ┌─────────────────────────────┐
│ send: 8B头 + 游戏数据        │ ──首响应──▶ │ send: 仅游戏数据（不带头）   │
│ 每次重发都带头（首包丢失自愈）│           │ 换房间 = 换源端口后回到未确认 │
└─────────────────────────────┘           └─────────────────────────────┘
```

改造点：

1. **发送封装**：`send_to_room(room_ip, room_port, payload)` 内部在"未确认"时前置 8B 头，"已确认"后不再前置；
2. **确认信号**：以收到目标房间的**首个响应包**为"已确认"（游戏协议通常有握手/首响应，天然复用，无需新机制）；
3. **首包丢失容错**：未确认期间每次重发都带同样的 8B 头，Envoy 端 `header_handled_` 未置位时可随时建会话；
4. **换房间**：Envoy 会话绑定客户端四元组，换房间必须**更换源端口**（新本地 socket → 新四元组 → 新会话），然后重新走"未确认"流程；
5. **会话超时**：udp_proxy 会话有 idle 超时，超时后再发包视为新会话（若未带头会被丢弃），客户端需周期性检测房间响应活性。

### 13.3 TCP 客户端改造

改造点：

1. **连接建立后先发头**：`connect()` 成功后、发送任何游戏数据前，先发 8B 头（建议与首个游戏数据拼接为一次 `send`）；
2. **只需一次**：头部只在连接最前端，后续所有数据不再带头；
3. **重连**：连接断开重连后，新连接需重新带头；
4. **无确认状态机**：TCP 可靠有序，连接建立即"会话建立"，无需"确认前/后"区分。

### 13.4 业务层决策逻辑（带还是不带）

> 客户端发送每个包时，只需回答一个布尔问题即可决定带不带 8B 头。两个协议的决策信号不同，但共同原则一致：**只在需要让 Envoy 建立/确认路由的阶段带头，路由就绪后不带头**。

**UDP —— 决策信号：是否收到过房间首响应**

```
发一个数据包前：这个会话是否已收到过房间服务器的任何响应？

  没收到 → 带头（8B头 + 游戏数据）
  收到过 → 不带头（纯游戏数据）
```

```cpp
bool confirmed = false; // 申请到房间后、首次发包前为 false

// 每次发送前：
bool with_header = !confirmed;

// 每次收到房间服务器的响应包后：
confirmed = true;
```

- **"首响应"定义**：来自目标房间的**第一个回包**（游戏协议的握手回复、进房确认、首个状态帧等，天然复用，无需新机制）；收到任意一个即确认会话已建立、路由已生效；
- **首次发包/首包丢失重发**：都带头——Envoy 端 `header_handled_` 未置位时，任何重发的带头包都能自愈建会话；
- **确认后正常发包**：不带头——省 8 字节/包（游戏 UDP 小包高频，头占比可达 20%+）；
- **换房间**：换源端口 → 新四元组 → 新会话 → 回到未确认 → 重新带头；
- **确认后误带头的后果**：Envoy 已置 `header_handled_`，8B 头被**透传**给房间服务器 → 数据错位、游戏协议解析失败，客户端状态机必须严格。

**TCP —— 决策信号：这个连接是否已发过数据**

```
连接建立后，第一次写数据前：这个连接发过数据没有？

  没发过 → 带头（建议 头+首个游戏数据 拼一次 send）
  发过   → 不带头
```

```cpp
bool header_sent = false; // connect 成功后为 false

// 每次发送前：
bool with_header = !header_sent;
if (with_header) {
  header_sent = true; // 只带一次
}

// 重连成功后：
header_sent = false; // 新连接必须重新带头
```

- **为何只带一次**：TCP 可靠有序，连接建立 = 会话建立，Envoy 收到首包即完成路由，无"确认"等待期；
- **为何重连要重置**：重连 = 新连接 = 新会话，`header_sent` 必须复位，否则新会话首包无头被当畸形关闭。

### 13.5 契约清单（客户端 ↔ Envoy 必须对齐）

| 项 | 契约 |
|---|---|
| 头部字节序 | RoomIP 网络序、RoomPort 大端 |
| UDP 带头规则 | 默认（forward_header=true）：首包带头选路，头部透传给上游；确认后可不带头，或始终带头（每包头部透传，需上游协议兼容）。forward_header=false：收到房间首响应前带头，之后不带头（头部被剥离） |
| UDP 换房间 | 更换源端口（新四元组） |
| TCP 带头规则 | 连接最前端一次性带头 |
| 响应方向 | 房间服务器回包**不带**头部，客户端按原游戏协议解析 |
| 畸形包处理 | UDP 丢弃、TCP 关连接（客户端需自行重试） |

---

## 14. 结论

- 方案基于 Envoy 1.39.0 源码验证，动态路由时序（阻断 → 首包解析 → 续链）完全可行；
- 共享 Parser + UDP/TCP 双适配器架构，一次实现双协议支持，扩展新协议仅需新增适配器；
- 客户端契约（UDP 确认前带头 / TCP 连接首部带头）与 Envoy 会话模型对齐后，动态房间路由闭环成立；
- 实施顺序建议：共享 parser → UDP 适配器（核心需求闭环）→ TCP 适配器 → 集成验证。
