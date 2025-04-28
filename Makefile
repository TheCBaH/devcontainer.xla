all: configure build

xla.configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc

configure: xla.configure
	cp -vpf $(addprefix xla/,.bazelversion .bazelrc *.bazelrc WORKSPACE) .
	git apply <WORKSPACE.patch

#BAZEL=set -eux;cd xla;bazel --output_base ${CURDIR}/.cache/bazel
BAZEL=bazel --output_base ${CURDIR}/.cache/bazel
BAZEL_OPTS=--repository_cache=${CURDIR}/.cache/bazel-repo --disk_cache=${CURDIR}/.cache/bazel-build

TARGET.pjrt=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so
TARGET.builder=//xla/hlo/builder:xla_builder
TARGET=//cpp:hlo_example

BAZEL_BUILD_OPTS=${BAZEL_OPTS} --define use_stablehlo=true

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGET}

%.build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} $(TARGET.$(basename $@))

build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} ${TARGET}

run:
	${BAZEL} run ${BAZEL_BUILD_OPTS} ${TARGET}

#build: fetch pjrt.build builder.build


.PHONY:\
 %.build\
 build\
 configure\
 pjrt.build\
 builder.build\
 fetch\
 xla.configure\