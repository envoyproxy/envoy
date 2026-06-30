// eBPF datapath for the sockmap socket interface. The sock_ops program adds established local
// sockets to a sockhash keyed by the connection tuple. The sk_msg program looks up the peer of
// each send and redirects the payload straight to its ingress queue, bypassing the TCP/IP stack.
// A peer that is not on the same host is absent from the map and falls back to TCP/IP.

#include <linux/bpf.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define AF_INET 2

// Connection tuple key shared with the user space registration path. The layout must stay in sync
// with SockKey in bpf_datapath.h.
struct sock_key {
  __u32 sip4;
  __u32 dip4;
  __u8 family;
  __u8 pad1;
  __u16 pad2;
  __u32 sport;
  __u32 dport;
};

// BTF map definition. struct bpf_map_def was removed in libbpf 1.0, so the map uses the BTF .maps
// form supported since libbpf 0.6. The user space loader overrides max_entries with the configured
// value before bpf_object__load, so the value here is only a nonzero placeholder that lets the
// section parse.
struct {
  __uint(type, BPF_MAP_TYPE_SOCKHASH);
  __uint(max_entries, 65536);
  __type(key, struct sock_key);
  __type(value, int);
} envoy_sockhash SEC(".maps");

// Builds the key for the socket owning skops. Ports are stored network order in the low 16 bits. The
// kernel exposes local_port host order in the low 16 bits, so it is converted with htons, and
// remote_port network order in the high 16 bits, so it is normalized down with htons(ntohl()).
static __always_inline void sockops_key(struct bpf_sock_ops *skops, struct sock_key *key) {
  key->sip4 = skops->local_ip4;
  key->dip4 = skops->remote_ip4;
  // Fixed IPv4 tag that must match the value the user space registration path writes.
  key->family = 1;
  key->pad1 = 0;
  key->pad2 = 0;
  key->sport = bpf_htons(skops->local_port);
  key->dport = bpf_htons(bpf_ntohl(skops->remote_port));
}

SEC("sockops")
int envoy_sockops(struct bpf_sock_ops *skops) {
  if (skops->family != AF_INET) {
    return 0;
  }
  switch (skops->op) {
  case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
  case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB: {
    struct sock_key key = {};
    sockops_key(skops, &key);
    bpf_sock_hash_update(skops, &envoy_sockhash, &key, BPF_NOEXIST);
    break;
  }
  default:
    break;
  }
  return 0;
}

SEC("sk_msg")
int envoy_sk_msg(struct sk_msg_md *msg) {
  if (msg->family != AF_INET) {
    return SK_PASS;
  }
  // The peer key swaps local and remote so the lookup finds the socket on the other end of the hop.
  // Ports stay network order in the low 16 bits, matching the sock_ops and user space keys, so the
  // high 16 bit remote_port is normalized down with htons(ntohl()).
  struct sock_key key = {
      .sip4 = msg->remote_ip4,
      .dip4 = msg->local_ip4,
      .family = 1,
      .sport = bpf_htons(bpf_ntohl(msg->remote_port)),
      .dport = bpf_htons(msg->local_port),
  };
  bpf_msg_redirect_hash(msg, &envoy_sockhash, &key, BPF_F_INGRESS);
  return SK_PASS;
}

char _license[] SEC("license") = "Dual BSD/GPL";
