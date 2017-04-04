set -e

wget https://github.com/sakra/cotire/archive/cotire-1.7.8.tar.gz
tar xf cotire-1.7.8.tar.gz
rsync -av cotire-cotire-1.7.8 $THIRDPARTY_SRC
