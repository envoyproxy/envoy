#!/bin/bash

python <(cat recipes.bzl; echo "print ' '.join(\"%s.dep\" % r for r in RECIPES)")
