#!/usr/bin/env bash
#
# Example setup network namespace for testing Envoy original_dst cluster
# Clean up with the cleanup script with the same arguments.
#
# Test with:
# $sudo ip netns exec ${NETNS} curl -v ${TARGET_IP}:80
#
set -e

# name of the network namespace
NETNS=$1

# IP address or prefix that will be redirected
TARGET_IP=$2

# Local Envoy Listener port number
ENVOY_PORT=10000

# Create veth pair
ip link add "$NETNS-veth0" type veth peer name "$NETNS-veth1"
ifconfig "$NETNS-veth0" 10.0.200.2/24 up

# Create network namespace
ip netns add "$NETNS"
# Move veth peer to the namespace
ip link set "$NETNS-veth1" netns "$NETNS"

# Configure network namespace
ip netns exec "$NETNS" ifconfig lo 127.0.0.1 up
ip netns exec "$NETNS" ifconfig "$NETNS-veth1" 10.0.200.1/24 up
ip netns exec "$NETNS" ip route add default via 10.0.200.2

#configure iptables REDIRECT in the PREROUTING hook of the root name space nat table.
iptables -t nat -I PREROUTING --src 0/0 --dst "$TARGET_IP" -p tcp --dport 80 -j REDIRECT --to-ports "$ENVOY_PORT"
