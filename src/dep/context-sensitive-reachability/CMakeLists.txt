cmake_minimum_required(VERSION 3.22)
project(cs_indexing)

set(CMAKE_CXX_STANDARD 11)
set(CS_FILES
        Graph.cpp
        GraphUtil.cpp
        Query.cpp
        bit_vector.cpp
        Grail.cpp
        TCSEstimator.cpp
        ReachBackbone.cpp
        PathTree.cpp
        DWGraph.cpp
        DWGraphUtil.cpp
        DataComp.cpp
        PathTree.cpp
        ProgressBar.cpp
        Tabulation.cpp
    )

add_executable(csr main.cpp ${CS_FILES})
target_include_directories(csr PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(csr PRIVATE fmt::fmt)

add_library(libcsr ${CS_FILES})
target_include_directories(libcsr PUBLIC ${PROJECT_SOURCE_DIR})
