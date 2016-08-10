#!/bin/bash

SCRIPT_DIR=`dirname $0`
BUILD_DIR=build/docs

if [ ! -d $BUILD_DIR/venv ]; then
  virtualenv $BUILD_DIR/venv --no-site-packages
  $BUILD_DIR/venv/bin/pip install -r $SCRIPT_DIR/requirements.txt
fi

source $BUILD_DIR/venv/bin/activate
cp -r $SCRIPT_DIR/landing_generated/* $1
sphinx-build -W -b html $SCRIPT_DIR $1/docs
