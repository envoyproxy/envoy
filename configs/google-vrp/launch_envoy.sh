#!/usr/bin/env bash

cd /etc/envoy || exit
envoy "$@"
