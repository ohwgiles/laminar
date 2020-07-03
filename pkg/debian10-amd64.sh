#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty)-1~upstream-debian10

DOCKER_TAG=$(docker build -q - <<EOS
FROM debian:10-slim
RUN apt-get update && apt-get install -y wget cmake g++ capnproto libcapnp-dev rapidjson-dev libsqlite3-dev libboost-dev zlib1g-dev
EOS
)

docker run --rm -i -v $SOURCE_DIR:/laminar:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS

mkdir /build
cd /build

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLAMINAR_VERSION=$VERSION -DZSH_COMPLETIONS_DIR=/usr/share/zsh/functions/Completion/Unix /laminar
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
Depends: libcapnp-0.7.0, libsqlite3-0, zlib1g
Description: Lightweight Continuous Integration Service
EOF
echo /etc/laminar.conf > laminar/DEBIAN/conffiles
cat <<EOF > laminar/DEBIAN/postinst
#!/bin/bash
echo Creating laminar user with home in /var/lib/laminar
useradd -r -d /var/lib/laminar -s /usr/sbin/nologin laminar
mkdir -p /var/lib/laminar/cfg/{jobs,contexts,scripts}
chown -R laminar: /var/lib/laminar
EOF
chmod +x laminar/DEBIAN/postinst

dpkg-deb --build laminar
mv laminar.deb /output/laminar_${VERSION}_amd64.deb
EOS
