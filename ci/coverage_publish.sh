#!/bin/bash

echo "Coverage"
echo $(pwd)

echo "NOTE: envoy build dir $ENVOY_BUILD_DIR"
ls -laR $ENVOY_BUILD_DIR

echo "NOTE: envoy build dir $TRAVIS_BUILD_DIR"
ls -laR $TRAVIS_BUILD_DIR
