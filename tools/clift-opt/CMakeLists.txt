#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(revng-clift-opt Main.cpp)

get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)

set(MLIR_LIBRARIES ${dialect_libs} ${conversion_libs} MLIROptLib)

target_link_libraries(revng-clift-opt MLIRCliftDialect ${MLIR_LIBRARIES}
                      ${LLVM_LIBRARIES})
