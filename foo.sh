#!/bin/bash
# to be copied to /tmp/envoy-docker-build/tmp/_bazel_bazel/

testdir=execroot/envoy/bazel-out/local-dbg/bin/external/envoy/test/coverage/coverage_tests.runfiles/envoy/bazel-out/local-dbg/bin/external/


prefix=/build/tmp/_bazel_bazel/
#prefix=/tmp/envoy-docker-build/tmp/_bazel_bazel  # uncomment to run outside of docker
cd $testdir;
for file in `find * |grep gcda`;
  do
    full_filename=`echo $file | sed s/gcda/gcno/`;
      shortname=$full_filename
      source_prefix="$prefix/400406edc57d332f0b9b805d2b8e33a1/execroot/envoy/bazel-out/local-dbg/bin/external/";
      if [ -f $source_prefix/$shortname ] ;
        then cp $source_prefix/$shortname `dirname $full_filename`;
      else
        echo "Unable to find gcno for $file"
        echo "$prefix/$file"
      fi
  done


