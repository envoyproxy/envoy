#!/usr/bin/env bash
JAEGER_VERSION=v0.4.2
curl -Lo /usr/local/lib/libjaegertracing_plugin.so https://github.com/jaegertracing/jaeger-client-cpp/releases/download/$JAEGER_VERSION/libjaegertracing_plugin.linux_amd64.so
