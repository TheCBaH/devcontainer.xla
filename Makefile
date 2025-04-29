all: configure build

xla.configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc

configure: xla.configure
	cp -vpf $(addprefix xla/,.bazelversion .bazelrc *.bazelrc WORKSPACE) .
	git apply <WORKSPACE.patch
	git -C xla apply <cpu_client_test.patch
	git -C xla apply <pjrt_c_api_client.patch

BAZEL=set -eux;cd xla;bazel --output_base ${CURDIR}/.cache/bazel
#BAZEL=bazel --output_base ${CURDIR}/.cache/bazel
BAZEL_OPTS=--repository_cache=${CURDIR}/.cache/bazel-repo --disk_cache=${CURDIR}/.cache/bazel-build

TARGET.pjrt=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so
TARGET.builder=//xla/hlo/builder:xla_builder
#TARGET=//cpp:hlo_example
#TARGET=@xla//xla/pjrt/c:pjrt_c_api_cpu_plugin.so @xla//xla/examples/axpy:stablehlo_compile_test
TARGET=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so //xla/examples/axpy:stablehlo_compile_test

BAZEL_BUILD_OPTS=${BAZEL_OPTS} --define use_stablehlo=true

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGET}

%.build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} $(TARGET.$(basename $@))

build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} ${TARGET}

run:
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/examples/axpy:stablehlo_compile_test 
	cp -v xla/bazel-bin/xla/examples/axpy/stablehlo_compile_test.runfiles/xla/*.mlir.bc hlo/
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/c:pjrt_c_api_cpu_test
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/cpu:cpu_client_test
	cp -v xla/bazel-bin/xla/pjrt/cpu/cpu_client_test.runfiles/xla/*.pb hlo/

#build: fetch pjrt.build builder.build
patches:
	git -C xla diff xla/pjrt/cpu > cpu_client_test.patch
	git -C xla diff xla/pjrt/pjrt_c_api_client.cc > pjrt_c_api_client.patch

.PHONY:\
 %.build\
 build\
 builder.build\
 configure\
 fetch\
 patches\
 pjrt.build\
 xla.configure\