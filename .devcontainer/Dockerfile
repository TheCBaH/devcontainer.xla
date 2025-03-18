ARG VARIANT=24.04
FROM ubuntu:${VARIANT}
RUN set -eux;\
    userdel -r ubuntu;\
    apt-get update;\
    export DEBIAN_FRONTEND=noninteractive; apt-get install -y --no-install-recommends\
  build-essential\
  ca-certificates\
  clang\
  curl\
  gdb\
  git\
  python3\
  unzip\
  valgrind\
  wget\
  xz-utils\
;\
    apt-get autoremove -y; apt-get clean -y ; apt-get clean; rm -rf /var/lib/apt/lists/*;\
    cd /tmp/;BAZEL=7.4.1;\
    wget https://github.com/bazelbuild/bazel/releases/download/$BAZEL/bazel-$BAZEL-installer-linux-x86_64.sh;\
    bash bazel-$BAZEL-installer-linux-x86_64.sh --prefix=/usr/local/;cd /;rm -rf /tmp/*;\
    echo DONE
