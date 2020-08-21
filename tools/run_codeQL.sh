#!/bin/bash

BUILD_TARGET=$(tools/codeQL.py)
echo "printing the output"
echo $BUILD_TARGET