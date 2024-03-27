revng_add_analyses_library(
  revngcMLIRPipes revngc MLIRPipe.cpp MLIRContainer.cpp
  ImportLLVMToMLIRPipe.cpp ImportCliftTypesPipe.cpp)

target_link_libraries(
  revngcMLIRPipes
  PRIVATE MLIRCliftDialect
  PUBLIC revng::revngPipeline
         revng::revngPipes
         MLIRTransforms
         MLIRDialect
         MLIRIR
         MLIRLLVMDialect
         MLIRDLTIDialect
         MLIRLLVMIRToLLVMTranslation
         revng::revngEarlyFunctionAnalysis
  PRIVATE MLIRCliftDialect MLIRBytecodeReader MLIRBytecodeWriter)
