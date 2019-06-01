# Need to remove -Werror when using SVF source code
string(REPLACE " -Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(SVF_SOURCES
    ${SVF_PATH}/lib/Util/AnalysisUtil.cpp
    ${SVF_PATH}/lib/Util/CPPUtil.cpp
    ${SVF_PATH}/lib/Util/DataFlowUtil.cpp
    ${SVF_PATH}/lib/Util/PTACallGraph.cpp
    ${SVF_PATH}/lib/Util/RaceAnnotator.cpp
    ${SVF_PATH}/lib/Util/ThreadCallGraph.cpp
    ${SVF_PATH}/lib/Util/BreakConstantExpr.cpp
    ${SVF_PATH}/lib/Util/Conditions.cpp
    ${SVF_PATH}/lib/Util/ExtAPI.cpp
    ${SVF_PATH}/lib/Util/PathCondAllocator.cpp
    ${SVF_PATH}/lib/Util/PTAStat.cpp
    ${SVF_PATH}/lib/Util/ThreadAPI.cpp
    ${SVF_PATH}/lib/Util/SVFModule.cpp
    ${SVF_PATH}/lib/MemoryModel/ConsG.cpp
    ${SVF_PATH}/lib/MemoryModel/LocationSet.cpp
    ${SVF_PATH}/lib/MemoryModel/LocMemModel.cpp
    ${SVF_PATH}/lib/MemoryModel/MemModel.cpp
    ${SVF_PATH}/lib/MemoryModel/PAGBuilder.cpp
    ${SVF_PATH}/lib/MemoryModel/PAG.cpp
    ${SVF_PATH}/lib/MemoryModel/CHA.cpp
    ${SVF_PATH}/lib/MemoryModel/PointerAnalysis.cpp
    ${SVF_PATH}/lib/MSSA/MemPartition.cpp
    ${SVF_PATH}/lib/MSSA/MemRegion.cpp
    ${SVF_PATH}/lib/MSSA/MemSSA.cpp
    ${SVF_PATH}/lib/MSSA/SVFGBuilder.cpp
    ${SVF_PATH}/lib/MSSA/SVFG.cpp
    ${SVF_PATH}/lib/MSSA/SVFGOPT.cpp
    ${SVF_PATH}/lib/MSSA/SVFGStat.cpp
    ${SVF_PATH}/lib/SABER/DoubleFreeChecker.cpp
    ${SVF_PATH}/lib/SABER/FileChecker.cpp
    ${SVF_PATH}/lib/SABER/LeakChecker.cpp
    ${SVF_PATH}/lib/SABER/ProgSlice.cpp
    ${SVF_PATH}/lib/SABER/SaberAnnotator.cpp
    ${SVF_PATH}/lib/SABER/SaberCheckerAPI.cpp
    ${SVF_PATH}/lib/SABER/SaberSVFGBuilder.cpp
    ${SVF_PATH}/lib/SABER/SrcSnkDDA.cpp
    ${SVF_PATH}/lib/WPA/Andersen.cpp
    ${SVF_PATH}/lib/WPA/AndersenLCD.cpp
    ${SVF_PATH}/lib/WPA/AndersenStat.cpp
    ${SVF_PATH}/lib/WPA/AndersenWave.cpp
    ${SVF_PATH}/lib/WPA/AndersenWaveDiff.cpp
    ${SVF_PATH}/lib/WPA/AndersenWaveDiffWithType.cpp
    ${SVF_PATH}/lib/WPA/FlowSensitive.cpp
    ${SVF_PATH}/lib/WPA/FlowSensitiveStat.cpp
    ${SVF_PATH}/lib/WPA/TypeAnalysis.cpp
    ${SVF_PATH}/lib/WPA/WPAPass.cpp)

include_directories(${SVF_PATH}/include)

add_llvm_loadable_module(fuzzalloc-svf-analysis ${SVF_SOURCES} SVFAnalysis.cpp
                         PLUGIN_TOOL opt)
