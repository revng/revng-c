revng_add_analyses_library(revngcMLIRPipes revngc MLIRPipe.cpp
                           MLIRContainer.cpp ImportLLVMToMLIRPipe.cpp)

target_link_libraries(
  revngcMLIRPipes
  PUBLIC revng::revngPipeline
         revng::revngPipes
         MLIRTransforms
         MLIRDialect
         MLIRIR
         MLIRLLVMDialect
         MLIRDLTIDialect
         MLIRLLVMIRToLLVMTranslation
  PRIVATE MLIRCliftDialect MLIRBytecodeReader MLIRBytecodeWriter)
