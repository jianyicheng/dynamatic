cmake_minimum_required(VERSION 3.0)
#cmake_policy(SET CMP0057 NEW)
set(LLVM_ROOT "" CACHE PATH "Root of LLVM install.")
list(APPEND CMAKE_PREFIX_PATH
     "${LLVM_ROOT}/lib/cmake/llvm")
list(APPEND CMAKE_PREFIX_PATH
     "${LLVM_ROOT}/lib/cmake/clang")
list(APPEND CMAKE_PREFIX_PATH
     "${LLVM_ROOT}/lib/cmake/polly")
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(Polly REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
add_definitions(${LLVM_DEFINITIONS})
add_definitions(-DSOURCE_DIR="${PROJECT_SOURCE_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

SET(CMAKE_EXE_LINKER_FLAGS "-static")

# TODO : Set up Polly include
include_directories(${LLVM_ROOT}/../polly/include)
include_directories(${LLVM_ROOT}/../polly/lib/External/isl/include)
include_directories(${LLVM_ROOT}/tools/polly/lib/External/isl/include/)

#link_libraries(/lib64/graphviz/libgvplugin_core.so)
#link_libraries(/lib64/graphviz/libgvplugin_dot_layout.so) 
#link_libraries(/lib64/graphviz/libgvplugin_neato_layout.so)
#link_libraries(/lib64/graphviz/libgvplugin_gd.so)
#link_libraries(/lib64/graphviz/libgvplugin_pango.so)


include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/OptimizeBitwidth)
include_directories(${CMAKE_SOURCE_DIR}/include/ElasticPass)

#include_directories(/usr/usr/include/graphviz)

add_subdirectory(ElasticPass)
add_subdirectory(MemElemInfo)
add_subdirectory(OptimizeBitwidth)
add_subdirectory(MyCFGPass)
add_subdirectory(FrequencyCounterPass)
add_subdirectory(FrequencyDataGatherPass)
# add_subdirectory(lib/Transforms/tests)

#add_subdirectory(lib)
  
