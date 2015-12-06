#!/bin/bash

VERSION=0.1

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]}))

#docker run --rm -i -v $SOURCE_DIR:/laminar:ro -v $OUTPUT_DIR:/output ubuntu bash -xe <<EOS
docker run -i -v $SOURCE_DIR:/laminar:ro -v $OUTPUT_DIR:/output ubuntu bash -xe <<EOS

apt-get update
apt-get install -y wget cmake g++ libsqlite3-dev libboost-filesystem1.55-dev

mkdir /build
cd /build

wget -O capnproto.tar.gz https://github.com/sandstorm-io/capnproto/archive/v0.5.3.tar.gz
wget -O websocketpp.tar.gz https://github.com/zaphoyd/websocketpp/archive/0.6.0.tar.gz
wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.0.2.tar.gz
md5sum -c <<EOF
909bd13ad6b8bc840ac78ab8f5bcb0a4  capnproto.tar.gz
5a485884c01f881aafbf1e055d851b82  websocketpp.tar.gz
97cc60d01282a968474c97f60714828c  rapidjson.tar.gz
EOF

tar xzf capnproto.tar.gz
tar xzf websocketpp.tar.gz
tar xzf rapidjson.tar.gz

cd /build/capnproto-0.5.3/c++/
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off .
make -j4
make install

cd /build/websocketpp-0.6.0/
cmake .
make install

cd /build/rapidjson-1.0.2/
cmake -DRAPIDJSON_BUILD_EXAMPLES=off .
make install

cd /build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ /laminar
make -j4
mkdir laminar
make DESTDIR=laminar install/strip

mkdir laminar/DEBIAN
cat <<EOF > laminar/DEBIAN/control
Package: laminar
Version: $VERSION
Section: 
Priority: optional
Architecture: amd64
Maintainer: Oliver Giles <web ohwg net>
Depends: libboost-filesystem1.55.0
Description: Lightweight Continuous Integration Service
EOF
cat <<EOF > laminar/DEBIAN/postinst
#!/bin/bash
echo Creating laminar user with home in /var/lib/laminar
useradd -r -d /var/lib/laminar -s /usr/sbin/nologin laminar
mkdir -p /var/lib/laminar/cfg/{jobs,nodes,scripts}
chown -R laminar: /var/lib/laminar
EOF
chmod +x laminar/DEBIAN/postinst

dpkg-deb --build laminar
mv laminar.deb /output/laminar_$VERSION-1_amd64.deb
EOS
