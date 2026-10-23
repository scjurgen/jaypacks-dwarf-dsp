# Cross build environment for the MOD Dwarf (moddwarf-new toolchain, GCC 9.4, glibc 2.27).
# Build (takes some time, needs network): docker build -f docker/Dockerfile -t dwarf-new docker
# Used by tools/jp.py build-dwarf (image name overridable with JP_DWARF_IMAGE).

FROM ubuntu:20.04

RUN useradd -ms /bin/bash modbuilder
RUN apt -y update && apt -y upgrade

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Berlin
RUN apt install -y tzdata

RUN apt install -y acl automake bc binutils bison build-essential bzip2 cpio curl cvs flex gawk git gperf gzip help2man libncurses-dev libtool libtool-bin mercurial nano ninja-build patch perl pkg-config python python3 rsync subversion tar texinfo unzip vim wget

USER modbuilder

WORKDIR /home/modbuilder
RUN git clone https://github.com/moddevices/mod-plugin-builder.git
WORKDIR /home/modbuilder/mod-plugin-builder
RUN ./bootstrap.sh moddwarf-new minimal

WORKDIR /home/modbuilder