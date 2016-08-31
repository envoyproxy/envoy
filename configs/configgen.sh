#!/bin/bash

SCRIPT_DIR=`dirname $0`
BUILD_DIR=$2
if [ ! -d $BUILD_DIR/venv ]; then
  virtualenv $BUILD_DIR/venv
  $BUILD_DIR/venv/bin/pip install -r $SCRIPT_DIR/requirements.txt
fi

mkdir -p $1
$BUILD_DIR/venv/bin/python $SCRIPT_DIR/configgen.py $1
