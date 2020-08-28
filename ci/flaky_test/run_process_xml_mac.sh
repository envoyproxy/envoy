#!/bin/bash

pip3 install slackclient
./ci/flaky_test/process_xml.py
