all: configure build

configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc

BAZEL=set -eux;cd xla;bazel --output_base ${CURDIR}/.cache/bazel
BAZEL_OPTS=--repository_cache=${CURDIR}/.cache/bazel-repo --disk_cache=${CURDIR}/.cache/bazel-build

TARGET=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGET}

build: fetch
	${BAZEL} build ${BAZEL_OPTS}  ${TARGET}
