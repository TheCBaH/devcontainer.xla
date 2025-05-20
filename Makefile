all: configure build

xla/configure:
	git submodule update --recursive --init --depth=1

xla.configure: xla/configure
	set -eux;cd xla;./configure.py --backend CPU $(if ${WITH_CLANG},--host_compiler CLANG --gcc_path /usr/bin/clang,--host_compiler GCC --gcc_path /usr/bin/gcc)

configure: xla.configure
	git -C xla checkout .
	git -C xla apply <cpu_client_test.patch
	git -C xla apply <pjrt_c_api_client.patch

BAZEL_CACHE_PERSISTENT=${CURDIR}/.cache/bazel
BAZEL_CACHE=${CURDIR}/.cache/bazel
BAZEL=set -eux;cd xla;bazel --output_base ${BAZEL_CACHE}
BAZEL_OPTS=$(if $(IDX_CHANNEL),,--repository_cache=${BAZEL_CACHE_PERSISTENT}-repo --disk_cache=${BAZEL_CACHE_PERSISTENT}-build)

TARGETS=\
 //xla/examples/axpy:stablehlo_compile_test\
 //xla/hlo/translate:xla-translate\
 //xla/hlo/translate:xla-translate-opt\
 //xla/pjrt/c:pjrt_c_api_cpu_plugin.so\
 //xla/pjrt/c:pjrt_c_api_cpu_test\
 //xla/pjrt/cpu:cpu_client_test\

BAZEL_BUILD_OPTS=${BAZEL_OPTS} --define use_stablehlo=true\
  $(if ${WITH_GDB} ,--compilation_mode dbg, --compilation_mode opt --strip=always)

fetch:
	${BAZEL} fetch ${BAZEL_OPTS} ${TARGETS}

build:
	${BAZEL} build ${BAZEL_BUILD_OPTS} ${TARGETS}

PROTOBUF_ROOTS=\
 xla/pjrt/proto/compile_options.proto\
 xla/pjrt/proto/execute_options.proto\
 xla/xla.proto\

PROTOBUF_FILES= ${PROTOBUF_ROOTS}\
 xla/autotune_results.proto\
 xla/autotuning.proto\
 xla/service/hlo.proto\
 xla/service/metrics.proto\
 xla/stream_executor/cuda/cuda_compute_capability.proto\
 xla/stream_executor/device_description.proto\
 xla/tsl/protobuf/dnn.proto\
 xla/xla_data.proto\

GOOGLE_PROTOBUF_FILES=\
 any.proto\
 duration.proto\
 timestamp.proto\
 wrappers.proto\

hlo.clean:
	git -C $(basename $@) clean -xdf .

run: hlo.clean run.exec run.protobuf

PROTOC=xla/bazel-bin/external/com_google_protobuf/protoc 
run.protobuf:
	${BAZEL} run ${BAZEL_BUILD_OPTS} @com_google_protobuf//:protoc
	mkdir -p hlo/google/protobuf
	cp -pv $(addprefix .cache/bazel/external/protobuf/src/google/protobuf/,${GOOGLE_PROTOBUF_FILES}) hlo/google/protobuf/
	set -eux;$(foreach d,$(sort $(dir $(PROTOBUF_FILES))), mkdir -p hlo/$d ;) true
	$(foreach f,$(PROTOBUF_FILES),\
 cp -v xla/$f hlo/$f;) true
	set -eux;$(foreach f,$(PROTOBUF_ROOTS),\
 ${PROTOC} --proto_path=hlo -o/dev/null hlo/$f;) true
	set -eux;$(foreach f, add.3x2 Identity.2x2,\
 ${PROTOC} --decode=xla.HloModuleProto --proto_path=hlo hlo/xla/xla.proto < hlo/${f}.xla.pb > hlo/${f}.xla.txt;) true
	${PROTOC} --decode=xla.CompileOptionsProto --proto_path=hlo xla/pjrt/proto/compile_options.proto < hlo/compile_options.0.pb > hlo/compile_options.0.txt
	set -eux;$(foreach f, add.3x2 Identity.2x2,\
 xla/bazel-bin/xla/hlo/translate/xla-translate --hlo-to-mlir-hlo hlo/${f}.xla.pb | xla/bazel-bin/xla/hlo/translate/xla-translate --mlir-hlo-to-hlo-text >hlo/${f}.txt;) true

run.exec:
	${BAZEL} build ${BAZEL_BUILD_OPTS} ${TARGET}
	rm -f hlo/pjrt_c_api_cpu_plugin.so
	cp -pv xla/bazel-bin/xla/pjrt/c/pjrt_c_api_cpu_plugin.so.runfiles/xla/xla/pjrt/c/pjrt_c_api_cpu_plugin.so hlo/
	chmod +w hlo/pjrt_c_api_cpu_plugin.so
	$(if ${WITH_GDB},,strip hlo/pjrt_c_api_cpu_plugin.so)
	cp -pv xla/xla/pjrt/c/pjrt_c_api.h hlo/
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/examples/axpy:stablehlo_compile_test 
	cp -pv $(addprefix xla/bazel-bin/xla/examples/axpy/stablehlo_compile_test.runfiles/xla/, *.mlir.bc *.pb) hlo/
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/c:pjrt_c_api_cpu_test
	${BAZEL} run ${BAZEL_BUILD_OPTS} //xla/pjrt/cpu:cpu_client_test
	cp -pv xla/bazel-bin/xla/pjrt/cpu/cpu_client_test.runfiles/xla/*.pb hlo/

patches:
	git -C xla diff xla/pjrt/cpu > cpu_client_test.patch
	git -C xla diff xla/pjrt/pjrt_c_api_client.cc > pjrt_c_api_client.patch

hlo:
	${MAKE} -C hlo run clean

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