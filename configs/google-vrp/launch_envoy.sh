#!/bin/bash

cd /etc/envoy || exit
envoy "$@"
