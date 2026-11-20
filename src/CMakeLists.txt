cmake_minimum_required(VERSION 3.22)
project(RefCntSolver)

set(CMAKE_CXX_STANDARD 17)
add_compile_options("-std=gnu++17")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# add_compile_options(-fsanitize=undefined)
# add_link_options(-fsanitize=undefined)

find_package(LLVM
        REQUIRED
        CONFIG
        PATHS ${LLVM_BUILD_PATH}
        NO_DEFAULT_PATH)
if (NOT LLVM_FOUND)
    message(FATAL_ERROR "can not find LLVMConfig.cmake under paths: ${LLVM_BUILD_PATH}")
else ()
    message(STATUS "Found LLVM : ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    message(STATUS "LLVM header files locate in : ${LLVM_INCLUDE_DIRS}")
    message(STATUS "LLVM definitions is : ${LLVM_DEFINITIONS}")

    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
    include(AddLLVM)
    include(HandleLLVMOptions)
    set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
    set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLVM_LDFLAGS}")

    include_directories(${LLVM_INCLUDE_DIRS})
    link_directories(${LLVM_LIBRARY_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
    message(STATUS "definitions in LLVM ${LLVM_DEFINITIONS}")
endif ()

function(CB_GET_SOURCE_FILES RETURN_LIST)
    file(GLOB ORIGINAL
            "*.c"
            "*.cpp"
            "*.cc")
    foreach (g ${ORIGINAL})
        get_filename_component(g ${g} NAME)
        list(APPEND FILES ${g})
    endforeach ()
    set(${RETURN_LIST} ${FILES} PARENT_SCOPE)
endfunction()

include_directories(third-party/rapidjson/include)
add_subdirectory(third-party)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include_directories(include)
add_subdirectory(dep)

add_subdirectory(src)
add_subdirectory(tool)