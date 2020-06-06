#!/bin/bash

set -e

if [ -n "${PACKAGE}" ]; then
    TARGET="package"
    CMAKE_OPTS="-DCPACK_GENERATOR=RPM"

    # Prepare rpmbuild dir
    mkdir -p ~/rpmbuild/SOURCES
    mkdir -p rpms

    dnf -y install \
        xmlto \
        systemd-devel
else
    TARGET="install"
fi

rm -rf thirdparty
mkdir -p thirdparty
pushd thirdparty

# Build & Install Criterion
if [ $(uname -m) != "armv6l" ] && ! pkg-config "criterion >= 2.3.1"; then
    git clone --recursive https://github.com/Snaipe/Criterion
    mkdir -p Criterion/build
    pushd Criterion/build
    git checkout v2.3.3
    cmake -DCMAKE_INSTALL_LIBDIR=/usr/local/lib64 ..
    if [ -z "${PACKAGE}" ]; then
        make -j$(nproc) install
    fi
    popd
fi

# Build & Install EtherLab
hg clone --branch stable-1.5 http://hg.code.sf.net/p/etherlabmaster/code etherlab
pushd etherlab
./bootstrap
./configure --enable-userlib=yes --enable-kernel=no
if [ -z "${PACKAGE}" ]; then
    make -j$(nproc) install
else
    wget https://etherlab.org/download/ethercat/ethercat-1.5.2.tar.bz2
    cp ethercat-1.5.2.tar.bz2 ~/rpmbuild/SOURCES
    rpmbuild -ba ethercat.spec
fi
popd

# Build & Install Fmtlib
if ! pkg-config "fmt >= 5.2.0"; then
    git clone --recursive https://github.com/fmtlib/fmt.git
    mkdir -p fmt/build
    pushd fmt/build
    git checkout 5.2.0
    cmake -DBUILD_SHARED_LIBS=1 ${CMAKE_OPTS} ..
    make -j$(nproc) ${TARGET}
    if [ -n "${PACKAGE}" ]; then
        cp fmt/build/*.rpm rpms
    fi
    popd
fi

# Build & Install spdlog
if ! pkg-config "spdlog >= 1.3.1"; then
    git clone --recursive https://github.com/gabime/spdlog.git
    mkdir -p spdlog/build
    pushd spdlog/build
    git checkout v1.3.1
    cmake -DCMAKE_BUILD_TYPE=Release -DSPDLOG_FMT_EXTERNAL=ON -DSPDLOG_BUILD_BENCH=OFF ${CMAKE_OPTS} ..
    make -j$(nproc) ${TARGET}
    if [ -n "${PACKAGE}" ]; then
        cp spdlog/build/*.rpm rpms
    fi
    popd
fi

# Build & Install libiec61850
if ! pkg-config "libiec61850 >= 1.3.1"; then
    git clone https://github.com/mz-automation/libiec61850
    mkdir -p libiec61850/build
    pushd libiec61850/build
    git checkout v1.3.1
    cmake -DCMAKE_INSTALL_LIBDIR=/usr/local/lib64 ${CMAKE_OPTS} ..
    make -j$(nproc) ${TARGET}
    if [ -n "${PACKAGE}" ]; then
        cp libiec61850/build/*.rpm rpms
    fi
    popd
fi

# Build & Install libwebsockets
if ! pkg-config "libwebsockets >= 2.3.0"; then
    git clone https://libwebsockets.org/repo/libwebsockets
    mkdir -p libwebsockets/build
    pushd libwebsockets/build
    git checkout v4.0-stable
    cmake -DCMAKE_INSTALL_LIBDIR=/usr/local/lib64 -DLWS_WITHOUT_EXTENSIONS=OFF ${CMAKE_OPTS} ..
    make -j$(nproc) ${TARGET}
    popd
fi

# Build & Install uldaq
if ! pkg-config "libuldaq >= 1.0.0"; then
    git clone https://github.com/stv0g/uldaq
    pushd uldaq
    git checkout rpmbuild
    autoreconf -i
    ./configure --enable-examples=no
    if [ -z "${PACKAGE}" ]; then
        make -j$(nproc) install
    else
        make dist
        cp fedora/uldaq_ldconfig.patch libuldaq-1.1.2.tar.gz ~/rpmbuild/SOURCES
        rpmbuild -ba fedora/uldaq.spec
    fi
    popd
fi

# Build & Install comedilib
if ! pkg-config "comedilib >= 0.11.0"; then
    git clone https://github.com/Linux-Comedi/comedilib.git
    pushd comedilib
    git checkout r0_11_0
    ./autogen.sh
    ./configure
    if [ -z "${PACKAGE}" ]; then
        make -j$(nproc) install
    else
        touch doc/pdf/comedilib.pdf # skip build of PDF which is broken..
        make dist
        cp comedilib-0.11.0.tar.gz ~/rpmbuild/SOURCES
        rpmbuild -ba comedilib.spec
    fi
    popd
fi

# Build & Install libre
if ! pkg-config "libre >= 0.5.6"; then
    git clone https://github.com/creytiv/re.git
    pushd re
    git checkout v0.6.1
    if [ -z "${PACKAGE}" ]; then
        make -j$(nproc) install
    else
        tar --transform 's|^\.|re-0.6.1|' -czvf ~/rpmbuild/SOURCES/re-0.6.1.tar.gz .
        rpmbuild -ba rpm/re.spec
    fi
    popd
fi

# Build & Install nanomsg
if ! pkg-config "nanomsg >= 1.0.0"; then
    git clone https://github.com/nanomsg/nanomsg.git
    pushd nanomsg
    cmake -DCMAKE_INSTALL_LIBDIR=/usr/local/lib64 ..
    if [ -z "${PACKAGE}" ]; then
        make -j$(nproc) install
    fi
    popd
fi

if [ -n "${PACKAGE}" ]; then
    cp ~/rpmbuild/RPMS/x86_64/*.rpm rpms
fi

popd
