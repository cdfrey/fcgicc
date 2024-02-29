# Override the default compiler flags for various build types
IF( CMAKE_CXX_COMPILER_ID MATCHES "GNU" )
	STRING( JOIN " " CMAKE_CXX_FLAGS_DEBUG_INIT -Og -ggdb3 -fno-omit-frame-pointer
		-Werror -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wsign-promo
		-Wshadow-compatible-local -Wnull-dereference -Wfloat-equal -Wduplicated-branches
		--std=c++2a -fno-strict-overflow -fno-delete-null-pointer-checks -fmax-errors=4 )
	STRING( JOIN " " CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT -O2 -ggdb3 -fno-omit-frame-pointer
		-Werror -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wsign-promo
		-Wshadow-compatible-local -Wnull-dereference -Wfloat-equal -Wduplicated-branches
		--std=c++2a -fno-strict-overflow -fno-delete-null-pointer-checks -fmax-errors=4 )
	STRING( JOIN " " CMAKE_CXX_FLAGS_RELEASE_INIT -O2
		-Werror -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wsign-promo
		-Wshadow-compatible-local -Wnull-dereference -Wfloat-equal -Wduplicated-branches
		--std=c++2a -fno-strict-overflow -fno-delete-null-pointer-checks -fmax-errors=4 )
	STRING( JOIN " " CMAKE_CXX_FLAGS_MINSIZEREL_INIT -Os
		-Werror -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wsign-promo
		-Wshadow-compatible-local -Wnull-dereference -Wfloat-equal -Wduplicated-branches
		--std=c++2a -fno-strict-overflow -fno-delete-null-pointer-checks -fmax-errors=4 )
ENDIF()

