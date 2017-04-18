#!/usr/bin/env python

import glob
import os

import util

def main():
    test_dir = os.path.join(os.environ['TEST_TMPDIR'], 'config_schemas_test')
    os.mkdir(test_dir)
    writer = util.TestWriter(test_dir)

    # test discovery and execution
    test_files = glob.glob(os.path.join(os.path.dirname(__file__), "test_*.py"))
    for test_file in test_files:
        module_name = os.path.splitext(os.path.basename(test_file))[0]
        __import__(module_name).test(writer)

if __name__ == '__main__':
    main()
