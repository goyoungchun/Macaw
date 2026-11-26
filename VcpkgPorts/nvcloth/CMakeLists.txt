cmake_minimum_required(VERSION 3.20)
project(nvcloth LANGUAGES CXX)

# PhysX 포트가 설치한 PxShared 헤더(foundation/*)를 사용합니다.
find_path(PXSHARED_INCLUDE_DIR NAMES foundation/PxVec4.h REQUIRED)

set(NvClothRoot "${CMAKE_CURRENT_SOURCE_DIR}/NvCloth")

# CPU 솔버, 플랫폼 원자 연산, 할당자, 패브릭 쿠커만 빌드합니다. CUDA와 DX11 솔버는 제외합니다.
file(GLOB NvClothCoreSources "${NvClothRoot}/src/*.cpp")
file(GLOB NvClothExtensionSources "${NvClothRoot}/extensions/src/*.cpp")
set(NvClothAvxSource "${NvClothRoot}/src/avx/SwSolveConstraints.cpp")

add_library(NvCloth SHARED
    ${NvClothCoreSources}
    ${NvClothExtensionSources}
    ${NvClothAvxSource}
    "${NvClothRoot}/src/ps/windows/PsWindowsAtomic.cpp"
    "${NvClothRoot}/include/NvCloth/ps/PsAllocator.cpp"
)

set_source_files_properties(${NvClothAvxSource} PROPERTIES COMPILE_OPTIONS "/arch:AVX")

target_include_directories(NvCloth PRIVATE
    "${PXSHARED_INCLUDE_DIR}"
    "${NvClothRoot}/include"
    "${NvClothRoot}/extensions/include"
    "${NvClothRoot}/src"
    "${NvClothRoot}/extensions/src"
)

target_compile_definitions(NvCloth PRIVATE
    NV_CLOTH_IMPORT=PX_DLL_EXPORT
    NV_CLOTH_ENABLE_CUDA=0
    NV_CLOTH_ENABLE_DX11=0
    WIN32
    WIN64
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    $<$<CONFIG:Debug>:PX_DEBUG=1>
    $<$<CONFIG:Debug>:PX_CHECKED=1>
)

target_compile_options(NvCloth PRIVATE /fp:fast /GF /MP /W3)

install(TARGETS NvCloth
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY "${NvClothRoot}/include/NvCloth" DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${NvClothRoot}/extensions/include/NvClothExt" DESTINATION include FILES_MATCHING PATTERN "*.h")
