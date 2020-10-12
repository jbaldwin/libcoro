.DEFAULT_GOAL := debug

# Builds the project and tests in the Debug directory.
debug:
	@$(MAKE) compile BUILD_TYPE=Debug --no-print-directory

# Builds the project and tests in the RelWithDebInfo directory.
release-with-debug-info:
	@$(MAKE) compile BUILD_TYPE=RelWithDebInfo --no-print-directory

# Builds the project and tests in the Release directory.
release:
	@$(MAKE) compile BUILD_TYPE=Release --no-print-directory

# Internal target for all build targets to call.
compile:
	mkdir -p ${BUILD_TYPE}; \
	cd ${BUILD_TYPE}; \
	cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ..; \
	cmake --build . -- -j $(nproc)

# Run Debug tests.
debug-test:
	@$(MAKE) test BUILD_TYPE=Debug --no-print-directory

# Run RelWithDebInfo tests.
release-with-debug-info-test:
	@$(MAKE) test BUILD_TYPE=RelWithDebInfo --no-print-directory

# Run Release tests.
release-test:
	@$(MAKE) test BUILD_TYPE=Release --no-print-directory

# Internal target for all test targets to call.
.PHONY: test
test:
	cd ${BUILD_TYPE}; \
	ctest -VV

# Cleans all build types.
.PHONY: clean
clean:
	rm -rf Debug
	rm -rf RelWithDebInfo
	rm -rf Release

# Runs clang-format with the project's .clang-format.
format:
	# Inlcude *.hpp|*.h|*.cpp but ignore catch lib as well as RelWithDebInfo|Release|Debug|build
	find . \( -name '*.hpp' -or -name '*.h' -or -name '*.cpp' \) 	\
		-and -not -name '*catch*' 									\
		-and -not -iwholename '*/RelWithDebInfo/*' 					\
		-and -not -iwholename '*/Release/*' 						\
		-and -not -iwholename '*/Debug/*' 							\
		-and -not -iwholename '*/build/*' 							\
		-exec clang-format -i --style=file {} \;
