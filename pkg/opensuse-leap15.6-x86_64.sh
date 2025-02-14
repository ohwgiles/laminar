#!/bin/bash -e

OUTPUT_DIR=$PWD

SOURCE_DIR=$(readlink -f $(dirname ${BASH_SOURCE[0]})/..)

VERSION=$(cd "$SOURCE_DIR" && git describe --tags --abbrev=8 --dirty | tr - .)~upstream_suse_leap_15.6

DOCKER_TAG=$(docker build -q - <<EOS
FROM opensuse/leap:latest
RUN zypper --non-interactive refresh && zypper --non-interactive update && zypper --non-interactive install -y rpm-build cmake make gcc-c++ wget sqlite3-devel boost-devel zlib-devel capnproto libcapnp-devel rapidjson-devel
EOS
)

# use /usr/sr/packages for opensuse instead of /root/rpmbuild
docker run --rm -i -v $SOURCE_DIR:/usr/src/packages/SOURCES/laminar-$VERSION:ro -v $OUTPUT_DIR:/output $DOCKER_TAG bash -xe <<EOS
mkdir /build
cd /build

cd
cat <<EOF > laminar.spec
Summary: Lightweight Continuous Integration Service
Name: laminar
Version: $VERSION
Release: 1
License: GPL
URL: https://laminar.ohwg.net/
BuildArch: x86_64
BuildRequires: cmake make gcc-c++ sqlite3-devel boost-devel zlib-devel capnproto libcapnp-devel rapidjson-devel

%description
Lightweight Continuous Integration Service

%prep

%build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLAMINAR_VERSION=$VERSION -DSYSTEMD_UNITDIR=%{_unitdir} %{_sourcedir}/laminar-$VERSION
pwd
make %{?_smp_mflags}

%install
%make_install

%files
%{_bindir}/laminarc
%{_sbindir}/laminard
%{_unitdir}/laminar.service
%config(noreplace) %{_sysconfdir}/laminar.conf
%{_datarootdir}/bash-completion/completions/laminarc
%{_datarootdir}/zsh/site-functions/_laminarc
%{_mandir}/man8/laminard.8.gz
%{_mandir}/man1/laminarc.1.gz

%post
echo Creating laminar user with home in %{_sharedstatedir}/laminar
useradd -r -d %{_sharedstatedir}/laminar -s %{_sbindir}/nologin laminar
mkdir -p %{_sharedstatedir}/laminar/cfg/{jobs,contexts,scripts}
chown -R laminar: %{_sharedstatedir}/laminar
EOF

rpmbuild -ba laminar.spec
mv /usr/src/packages/RPMS/x86_64/laminar-$VERSION-1.x86_64.rpm /output/
EOS
