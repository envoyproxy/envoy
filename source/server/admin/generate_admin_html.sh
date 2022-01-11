#!/bin/sh

echo '#include "absl/strings/str_replace.h"'
echo ''
echo 'constexpr absl::string_view AdminHtmlStart = R"EOF('
echo '<head>'
cat "$1"
echo '<style>'
cat "$2"
echo '</style>'
echo '<script>'
cat "$3"
echo '</script>'
echo '</head>'
echo ')EOF";'
