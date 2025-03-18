all: cpu

CPUS=$$(getconf _NPROCESSORS_ONLN)

CMAKE.cuda=-DGGML_NATIVE=OFF -DGGML_CUDA=ON
ENV.cuda=env PATH=$$PATH:/usr/local/cuda/bin

%.build:
	cd llama.cpp && $(ENV.$(basename $@)) cmake -B build.$(basename $@) -G Ninja -DCMAKE_BUILD_TYPE=Release $(CMAKE.$(basename $@)) -DLLAMA_CURL=ON
	cd llama.cpp && $(ENV.$(basename $@)) cmake --build build.$(basename $@) -j ${CPUS} --target llama-cli

LLAMA_CLI=llama.cpp/build.$(basename $@)/bin/llama-cli

MODEL=-hf bartowski/SmolLM2-135M-Instruct-GGUF
%.run:
	${LLAMA_CLI} --list-devices
	${LLAMA_CLI} ${MODEL} -p 'find x, when x = 1 + 2' -n 128 -no-cnv

%.clean:
	rm -rf llama.cpp/build.$(basename $@)

cpu: cpu.build cpu.run

cuda: cuda.build cuda.run

clean: cpu.clean cuda.clean
