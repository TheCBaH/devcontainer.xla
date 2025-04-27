all: configure build

configure:
	set -eux;cd xla;./configure.py --backend CPU --host_compiler GCC --gcc_path /usr/bin/gcc-10

build:
	bazel build --test_output=all --spawn_strategy=sandboxed //xla/...
