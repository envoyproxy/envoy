set -e

wget -O http-parser-v2.7.0.tar.gz https://github.com/nodejs/http-parser/archive/v2.7.0.tar.gz
tar xf http-parser-v2.7.0.tar.gz
cd http-parser-2.7.0
$CC -O2 -c http_parser.c -o http_parser.o
ar rcs libhttp_parser.a http_parser.o
cp libhttp_parser.a $THIRDPARTY_BUILD/lib
cp http_parser.h $THIRDPARTY_BUILD/include
