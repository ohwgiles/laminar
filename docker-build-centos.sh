#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]}))

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty | tr - .)

DOCKER_TAG=$(docker build -q - <<EOS
FROM centos:7
RUN yum -y install epel-release && yum -y install rpm-build cmake3 make gcc gcc-c++ wget sqlite-devel boost-devel zlib-devel
EOS
)

docker run --rm -i -v $SOURCE_DIR:/root/rpmbuild/SOURCES/laminar-$VERSION:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS

mkdir /build
cd /build

wget -O capnproto.tar.gz https://github.com/capnproto/capnproto/archive/3079784bfaf3ba05edacfc63d6d494b76a85a3a5.tar.gz
wget -O websocketpp.tar.gz https://github.com/zaphoyd/websocketpp/archive/0.7.0.tar.gz
wget -O rapidjson.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
md5sum -c <<EOF
c5c04c1892a381e30bd032a6bceef111  capnproto.tar.gz
5027c20cde76fdaef83a74acfcf98e23  websocketpp.tar.gz
badd12c511e081fec6c89c43a7027bce  rapidjson.tar.gz
EOF

tar xzf capnproto.tar.gz
tar xzf websocketpp.tar.gz
tar xzf rapidjson.tar.gz

cd /build/capnproto-3079784bfaf3ba05edacfc63d6d494b76a85a3a5/c++/
cmake3 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=off .
make -j4
make install

cd /build/websocketpp-0.7.0/
cmake3 .
make install

cd /build/rapidjson-1.1.0/
cmake3 -DRAPIDJSON_BUILD_EXAMPLES=off .
make install

cd
cat <<EOF > laminar.spec
Summary: Lightweight Continuous Integration Service
Name: laminar
Version: $VERSION
Release: 1
License: GPL
BuildRequires: systemd-units
Requires: sqlite boost-filesystem zlib

%description
Lightweight Continuous Integration Service

%prep

%build
cmake3 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ -DSYSTEMD_UNITDIR=%{_unitdir} %{_sourcedir}/laminar-$VERSION
pwd
make

%install
%make_install

%files
%{_bindir}/laminarc
%{_bindir}/laminard
%{_unitdir}/laminar.service
%config(noreplace) %{_sysconfdir}/laminar.conf

%post
echo Creating laminar user with home in %{_sharedstatedir}/laminar
useradd -r -d %{_sharedstatedir}/laminar -s %{_sbindir}/nologin laminar
mkdir -p %{_sharedstatedir}/laminar/cfg/{jobs,nodes,scripts}
chown -R laminar: %{_sharedstatedir}/laminar
EOF

rpmbuild -ba laminar.spec
mv rpmbuild/RPMS/x86_64/laminar-$VERSION-1.x86_64.rpm /output/
EOS
