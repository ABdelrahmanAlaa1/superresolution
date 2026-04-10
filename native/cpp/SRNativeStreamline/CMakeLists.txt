cmake_minimum_required(VERSION 3.15)
project(SuperResolutionStreamline)
message(STATUS "生成Streamline模块")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT ON_WINDOWS)
    message(FATAL_ERROR "${LIB_PLATFORM} 平台不支持Streamline (Windows only)")
endif()

add_definitions(-DON_WIN64)
add_definitions(-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

if(MSVC)
    add_compile_options(/utf-8 /ZI)
endif()

find_package(Vulkan REQUIRED)

# ── Streamline SDK paths ──
set(STREAMLINE_SDK_DIR "${PROJECT_SOURCE_DIR}/third_party/Streamline")
set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_SDK_DIR}/include")

link_directories(
    ${PROJECT_SOURCE_DIR}/libraries/${LIB_PLATFORM}
    ${PROJECT_SOURCE_DIR}/../SRNativeMain/libraries/${LIB_PLATFORM}
)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/../SRNativeMain/include
    ${PROJECT_SOURCE_DIR}/src
    ${STREAMLINE_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

# ── Sources ──
aux_source_directory(${PROJECT_SOURCE_DIR}/src ALL_SRC)
add_library(SR_STREAMLINE_LIB SHARED ${ALL_SRC})

set_target_properties(SR_STREAMLINE_LIB PROPERTIES
    OUTPUT_NAME "libSuperResolutionStreamline+${LIB_PLATFORM}+${SR_BUILD_TYPE}")

target_compile_definitions(SR_STREAMLINE_LIB PRIVATE
    SRLIB_VERSION="${SRLIB_VERSION}"
)

# Streamline uses dynamic loading (LoadLibraryW for sl.interposer.dll),
# so we don't link against interposer statically. We only need headers.
target_link_libraries(SR_STREAMLINE_LIB
    SR_MAIN_LIB
    ${Vulkan_LIBRARIES}
)
