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
