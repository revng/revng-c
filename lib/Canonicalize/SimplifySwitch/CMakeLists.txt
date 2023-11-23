#
# Copyright rev.ng Labs Srl. See LICENSE.md for details.
#

revng_add_analyses_library(revngcSimplifySwitch revngc SimplifySwitchPass.cpp
                           SimplifySwitchPipe.cpp)

target_link_libraries(
  revngcSimplifySwitch
  revngcSupport
  revng::revngABI
  revng::revngModel
  revng::revngSupport
  revng::revngPipes
  revng::revngLift
  ${LLVM_LIBRARIES})
