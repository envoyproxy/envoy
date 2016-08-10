openssl genrsa -out cakey.pem 1024
openssl req -new -key cakey.pem -out cacert.csr -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Test CA
test@lyft.com


EOF
openssl x509 -req -days 730 -in cacert.csr -sha256 -signkey cakey.pem -out cacert.pem

openssl genrsa -out serverkey.pem 1024
openssl req -new -key serverkey.pem -out servercert.csr -sha256  <<EOF
US
California
San Francisco
Lyft
Test
Test Server
test@lyft.com


EOF
openssl x509 -req -days 730 -in servercert.csr -sha256 -CA cacert.pem -CAkey cakey.pem -CAcreateserial -out servercert.pem

openssl genrsa -out clientkey.pem 1024
openssl req -new -key clientkey.pem -out clientcert.csr -sha256  <<EOF
US
California
San Francisco
Lyft
Test
Test Client
test@lyft.com


EOF
openssl x509 -req -days 730 -in clientcert.csr -sha256 -CA cacert.pem -CAkey cakey.pem -CAcreateserial -out clientcert.pem

openssl genrsa -out upstreamcakey.pem 1024
openssl req -new -key upstreamcakey.pem -out upstreamcacert.csr -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Test Upstream CA
test@lyft.com


EOF
openssl x509 -req -days 730 -in upstreamcacert.csr -sha256 -signkey upstreamcakey.pem -out upstreamcacert.pem

openssl genrsa -out upstreamkey.pem 1024
openssl req -new -key upstreamkey.pem -out upstreamcert.csr -config upstreamcert.cfg -batch -sha256

openssl x509 -req -days 730 -in upstreamcert.csr -sha256 -CA upstreamcacert.pem -CAkey upstreamcakey.pem -CAcreateserial -out upstreamcert.pem -extensions v3_req -extfile upstreamcert.cfg

rm *.csr
rm *.srl
