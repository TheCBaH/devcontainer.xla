all: configure build

configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc

BAZEL=set -eux;cd xla;bazel --output_base ${CURDIR}/.cache/bazel
BAZEL_OPTS=--repository_cache=${CURDIR}/.cache/bazel-repo --disk_cache=${CURDIR}/.cache/bazel-build

TARGET.pjrt=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so
TARGET.builder=//xla/hlo/builder:xla_builder

BAZEL_BUILD_OPTS=${BAZEL_OPTS} --define use_stablehlo=true

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGET.pjir} ${TARGET.builder}

%.build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} $(TARGET.$(basename $@))

build: fetch pjrt.build builder.build
