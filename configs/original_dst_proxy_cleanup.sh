#
# Cleanup network namespace after testing Envoy original_dst cluster
#

NETNS=$1
TARGET_IP=$2
ENVOY_PORT=10000

# remove iptables rule
iptables -t nat -D PREROUTING --src 0/0 --dst "$TARGET_IP" -p tcp --dport 80 -j REDIRECT --to-ports "$ENVOY_PORT"

# delete network namespace
ip netns delete "$NETNS"

# delete veth pair
ip link del "$NETNS-veth0" type veth peer name "$NETNS-veth1"
