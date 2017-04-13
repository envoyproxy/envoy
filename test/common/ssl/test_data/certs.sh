# Generate ca_key.pem and ca_cert.pem.
openssl req -new -key ca_key.pem -out ca_cert.csr -sha256 <<EOF
US
California
San Francisco
Lyft
Test
Test CA
test@lyft.com


EOF

openssl x509 -req -days 3650 -in ca_cert.csr -sha256 -signkey ca_key.pem -out ca_cert.pem

# Generate no_san_key.pem and no_san_cert.pem.
openssl req -new -key no_san_key.pem -out no_san_cert.csr -sha256  <<EOF
US
California
San Francisco
Lyft
Test
Test Client
test@lyft.com


EOF
openssl x509 -req -days 730 -in no_san_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out no_san_cert.pem

# Generate san_dns_key.pem and san_dns_cert.pem.
openssl req -new -key san_dns_key.pem -out san_dns_cert.csr -config san_dns_cert.cfg -batch -sha256

openssl x509 -req -days 730 -in san_dns_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_dns_cert.pem -extensions v3_req -extfile san_dns_cert.cfg

# Generate san_uri_key.pem and san_uri_cert.pem.
openssl req -new -key san_uri_key.pem -out san_uri_cert.csr -config san_uri_cert.cfg -batch -sha256

openssl x509 -req -days 730 -in san_uri_cert.csr -sha256 -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out san_uri_cert.pem -extensions v3_req -extfile san_uri_cert.cfg

rm *csr
rm *srl
