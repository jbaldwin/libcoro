# Internal target for all build targets to call.
# Cleans all build types.
.PHONY: clean
clean:
	rm -rf Debug
	rm -rf RelWithDebInfo
	rm -rf Release

# Runs clang-format with the project's .clang-format.
.PHONY: format
format:
	# Inlcude *.hpp|*.h|*.cpp but ignore catch lib as well as RelWithDebInfo|Release|Debug|build
	find . \( -name '*.hpp' -or -name '*.h' -or -name '*.cpp' \) 	\
		-and -not -name '*catch*' 									\
		-and -not -iwholename '*/RelWithDebInfo/*' 					\
		-and -not -iwholename '*/Release/*' 						\
		-and -not -iwholename '*/Debug/*' 							\
		-and -not -iwholename '*/build/*' 							\
		-and -not -iwholename '*/vendor/*'							\
		-exec clang-format -i --style=file {} \;
