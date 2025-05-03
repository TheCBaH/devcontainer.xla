all: configure build

xla.configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc

configure: xla.configure
	cp -vpf $(addprefix xla/,.bazelversion .bazelrc *.bazelrc WORKSPACE) .
	git apply <WORKSPACE.patch
	git -C xla apply <cpu_client_test.patch
	git -C xla apply <pjrt_c_api_client.patch
	sed -ie 's/build -c opt//' xla/tensorflow.bazelrc

BAZEL_CACHE_PERSISTENT=${CURDIR}/.cache/bazel
BAZEL_CACHE=${CURDIR}/.cache/bazel
BAZEL=set -eux;cd xla;bazel --output_base ${BAZEL_CACHE}
#BAZEL=bazel --output_base ${BAZEL_CACHE}
BAZEL_OPTS=$(if $(IDX_CHANNEL),,--repository_cache=${BAZEL_CACHE_PERSISTENT} --disk_cache=${BAZEL_CACHE_PERSISTENT}-build)

TARGET.pjrt=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so
TARGET.builder=//xla/hlo/builder:xla_builder
#TARGET=//cpp:hlo_example
#TARGET=@xla//xla/pjrt/c:pjrt_c_api_cpu_plugin.so @xla//xla/examples/axpy:stablehlo_compile_test
TARGET=//xla/pjrt/c:pjrt_c_api_cpu_plugin.so //xla/examples/axpy:stablehlo_compile_test

BAZEL_BUILD_OPTS=${BAZEL_OPTS} --define use_stablehlo=true --compilation_mode fastbuild --strip=always --copt -Os
# --subcommands  

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGET}

%.build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} $(TARGET.$(basename $@))

build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} ${TARGET}
	rm -f hlo/pjrt_c_api_cpu_plugin.so
	cp -pv xla/bazel-bin/xla/pjrt/c/pjrt_c_api_cpu_plugin.so.runfiles/xla/xla/pjrt/c/pjrt_c_api_cpu_plugin.so hlo/
	chmod +w hlo/pjrt_c_api_cpu_plugin.so
	$(if ${WITH_GDB},,strip hlo/pjrt_c_api_cpu_plugin.so)
	cp -pv xla/xla/pjrt/c/pjrt_c_api.h hlo/
	
run:
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/examples/axpy:stablehlo_compile_test 
	cp -pv xla/bazel-bin/xla/examples/axpy/stablehlo_compile_test.runfiles/xla/*.mlir.bc hlo/
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/c:pjrt_c_api_cpu_test
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/cpu:cpu_client_test
	cp -pv xla/bazel-bin/xla/pjrt/cpu/cpu_client_test.runfiles/xla/*.pb hlo/

#build: fetch pjrt.build builder.build
patches:
	git -C xla diff xla/pjrt/cpu > cpu_client_test.patch
	git -C xla diff xla/pjrt/pjrt_c_api_client.cc > pjrt_c_api_client.patch

hlo:
	${MAKE} -C hlo run

log:
	${BAZEL} info command_log


.PHONY:\
 %.build\
 build\
 builder.build\
 configure\
 fetch\
 hlo\
 log\
 patches\
 pjrt.build\
 xla.configure\