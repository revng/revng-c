#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_analyses_library(revngcTypeNames revngc LLVMTypeNames.cpp
                           ModelTypeNames.cpp ModelToPTMLTypeHelpers.cpp)

target_link_libraries(
  revngcTypeNames
  revngcSupport
  revng::revngABI
  revng::revngModel
  revng::revngPTML
  revng::revngSupport
  ${LLVM_LIBRARIES})
