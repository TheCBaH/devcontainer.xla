ARG VARIANT=11
FROM debian:${VARIANT}
RUN set -eux;\
    apt-get update;\
    export DEBIAN_FRONTEND=noninteractive; apt-get install -y --no-install-recommends\
  build-essential\
  ca-certificates\
  clang\
  cmake\
  cppcheck\
  curl\
  gdb\
  git\
  lldb\
  llvm\
  make\
  ninja-build\
  valgrind\
  wget\
  xz-utils\
;\
    apt-get autoremove -y; apt-get clean -y ; apt-get clean; rm -rf /var/lib/apt/lists/*;\
    echo DONE
