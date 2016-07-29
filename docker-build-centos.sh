#!/bin/bash

VERSION=0.3

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]}))

docker run --rm -i -v $SOURCE_DIR:/root/rpmbuild/SOURCES/laminar-$VERSION:ro -v $OUTPUT_DIR:/output centos bash -xe <<EOS

yum -y install rpm-build cmake make gcc gcc-c++ wget sqlite-devel boost-devel zlib-devel

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

cd
cat <<EOF > laminar.spec
Summary: Lightweight Continuous Integration Service
Name: laminar
Version: $VERSION
Release: 1
License: GPL
BuildRequires: systemd-units
Requires: boost-filesystem zlib

%description
Lightweight Continuous Integration Service

%prep

%build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ %{_sourcedir}/laminar-$VERSION
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
