set -e

wget https://github.com/gabime/spdlog/archive/v0.11.0.tar.gz
tar xf v0.11.0.tar.gz
rsync -av spdlog-0.11.0 $THIRDPARTY_SRC
