dhls=/workspace
user=$(if $(shell id -u),$(shell id -u),9001)
group=$(if $(shell id -g),$(shell id -g),1000)

build-docker: test-docker
	docker run -it -u $(user) -v $(shell pwd):/workspace dhls20:latest /bin/bash \
	-c "make build"
	echo "Dynamatic has been installed successfully!"

test-docker:
	(cd docker; docker build --build-arg UID=$(user) --build-arg GID=$(group) . --tag dhls20)

shell:
	docker run -it -u $(user) -v $(shell pwd):/workspace dhls20:latest /bin/bash

build:
	set -e # Abort if one of the commands fail
	# build LLVM
	mkdir -p $(dhls)/llvm/build
	(cd $(dhls)/llvm/build; \
	 cmake ../llvm -DLLVM_ENABLE_PROJECTS="clang;polly" \
	 -DLLVM_INSTALL_UTILS=ON -DLLVM_TARGETS_TO_BUILD="X86" \
	 -DLLVM_ENABLE_ASSERTIONS=ON \
	 -DLLVM_BUILD_EXAMPLES=OFF \
	 -DLLVM_ENABLE_RTTI=OFF \
	 -DLLVM_USE_LINKER=lld \
	 -DCMAKE_BUILD_TYPE=DEBUG || exit 1)
	(cd $(dhls)/llvm/build; \
	 make -j4 || exit 1)
	
	# build Dynamatic
	mkdir -p $(dhls)/dhls/elastic-circuits/build
	(cd $(dhls)/dhls/elastic-circuits/build; \
	 cmake .. \
	 -DLLVM_ROOT=../../../llvm/build \
	 -DCMAKE_BUILD_TYPE=DEBUG || exit 1)
	(cd $(dhls)/dhls/elastic-circuits/build; \
	 make -j4 || exit 1)
	
	# buffer
	mkdir -p $(dhls)/dhls/Buffers/bin
	(cd $(dhls)/dhls/Buffers; make || exit 1)
	# dot2vhdl
	mkdir -p $(dhls)/dhls/dot2vhdl/bin
	(cd $(dhls)/dhls/dot2vhdl; make || exit 1)

rebuild:
	set -e # Abort if one of the commands fail
	
	# build LLVM
	(cd $(dhls)/llvm/build; \
	 make -j4 || exit 1)
	
	# build Dynamatic
	(cd $(dhls)/elastic-circuits/build; \
	 make -j4 || exit 1)
	
clean:
	rm -rf $(dhls)/llvm/build
	rm -rf $(dhls)/elastic-circuits/build
	rm -rf $(dhls)/Buffers/bin
	rm -rf $(dhls)/dot2vhdl/bin

