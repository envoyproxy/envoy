#!/usr/bin/awk -f

# usage:
# bazel run -c opt --config=clang test/common/coroutine:perf_test  -- --benchmark_repetitions=5 --benchmark_report_aggregates_only=true | test/commom/coroutine/aggregate_perf_test_stats.awk

function test_name(input) {
  split(input, parts, "/");
  split(parts[4], r, "_");
  return parts[2] "-" parts[3] "-" r[1];
}

function print_stats(name, first, second) {
  printf "%-24s" "%8d µs\t" "%8d µs\t" "%6.2f%%\n", name, first, second, second / first * 100
}

BEGIN {
  cb_mean_total = 0;
  co_mean_total = 0;
  cb_std_total = 0;
  co_std_total = 0;

  cb_balanced_mean_total = 0;
  co_balanced_mean_total = 0;

  cb_slow_reader_mean_total = 0;
  co_slow_reader_mean_total = 0;

  cb_slow_writer_mean_total = 0;
  co_slow_writer_mean_total = 0;
  printf "%-24s" "%11s\t" "%11s\t" "%7s\n", "test_param", "cb_cpu", "coro_cpu", "co/cb"
}

/bmCallbackEcho.*_mean/{
  cb[test_name($1)] = int($4)
  cb_mean_total += int($4)
}

/bmCoroutineEcho.*_mean/{
  co[test_name($1)] = int($4)
  co_mean_total += int($4)
  print_stats(test_name($1), cb[test_name($1)], int($4))
}

/bmCallbackEcho.*_stddev/{
  cb_std_total += int($4)
}

/bmCoroutineEcho.*_stddev/{
  co_std_total += int($4)
}

/bmCallbackEcho\/balanced.*_mean/ {
  cb_balanced_mean_total += int($4)
}
/bmCoroutineEcho\/balanced.*_mean/ {
  co_balanced_mean_total += int($4)
}

/bmCallbackEcho\/slow_reader.*_mean/ {
  cb_slow_reader_mean_total += int($4)
}
/bmCoroutineEcho\/slow_reader.*_mean/ {
  co_slow_reader_mean_total += int($4)
}

/bmCallbackEcho\/slow_writer.*_mean/ {
  cb_slow_writer_mean_total += int($4)
}
/bmCoroutineEcho\/slow_writer.*_mean/ {
  co_slow_writer_mean_total += int($4)
}

END {
  print "--------"
  print_stats("balanced_total", cb_balanced_mean_total, co_balanced_mean_total)
  print_stats("slow_reader_total", cb_slow_reader_mean_total, co_slow_reader_mean_total)
  print_stats("slow_writer_total", cb_slow_writer_mean_total, co_slow_writer_mean_total)
  print_stats("total", cb_mean_total, co_mean_total)
  print_stats("std_dev", cb_std_total, co_std_total)
}
