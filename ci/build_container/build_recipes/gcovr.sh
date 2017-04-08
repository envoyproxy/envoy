set -e

wget -O gcovr-3.3.tar.gz https://github.com/gcovr/gcovr/archive/3.3.tar.gz
tar xf gcovr-3.3.tar.gz
rsync -av gcovr-3.3 $THIRDPARTY_SRC
rm gcovr-3.3.tar.gz
