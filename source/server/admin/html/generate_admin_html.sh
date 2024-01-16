#!/bin/sh

echo 'constexpr absl::string_view AdminHtmlStart = R"EOF('
cat "$1"
echo ')EOF";'
echo 'constexpr absl::string_view AdminCss = R"EOF('
cat "$2"
echo ')EOF";'
echo 'constexpr absl::string_view AdminActiveStatsJs = R"EOF('
cat "$3"
echo ')EOF";'
echo 'constexpr absl::string_view AdminActiveParamsHtml = R"EOF('
cat "$4"
echo ')EOF";'

# The Windows compiler complains histograms.js is too big to fit into a string
# constant, so generate the histograms implementation in two chunks. These will
# be combined in source/server/admin/admin_html_util.cc when constructing
# BuiltinResourceProvider. We do this even when not on Windows for consistency.
lines=$(wc -l < "$5")
first_lines=$((lines / 2))
next_lines=$((first_lines + 1))

echo 'constexpr absl::string_view HistogramsJs1 = R"EOF('
head --lines="$first_lines" "$5"
echo ')EOF";'

echo 'constexpr absl::string_view HistogramsJs2 = R"EOF('
tail --lines=+"$next_lines" "$5"
echo ')EOF";'
