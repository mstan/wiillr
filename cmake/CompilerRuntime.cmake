# SPDX-License-Identifier: MIT
# Export the selected compiler's actual search directories for runtime closure.
string(REPLACE ";" "\n" _wiillr_runtime_directories "${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES}")
file(WRITE "${CMAKE_BINARY_DIR}/release-runtime-directories.txt" "${_wiillr_runtime_directories}\n")
