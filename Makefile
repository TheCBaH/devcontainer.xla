all: configure build

configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc-10

build:
	set -eux;bazel build //xla/pjrt/c:pjrt_c_api_cpu_plugin.so
