vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

# 1.1.6에는 태그가 없어서 master 커밋으로 고정합니다.
vcpkg_from_github(
    OUT_SOURCE_PATH SourcePath
    REPO NVIDIAGameWorks/NvCloth
    REF 6e86b1838ae185c527744d475d34777ff69c1b8a
    SHA512 c41813cf51723a7e5824dfc924190f2748bfe1a0ae05c943b5276af6689bbbf505e831b49973fffd7e4c251692256dcc2ff633bae0254ee756eb31454398af5a
)

# 최신 MSVC에는 typeinfo.h가 없으므로 표준 헤더로 바꿉니다.
vcpkg_replace_string("${SourcePath}/NvCloth/include/NvCloth/ps/PsAllocator.h" "#include <typeinfo.h>" "#include <typeinfo>")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SourcePath}")

vcpkg_cmake_configure(SOURCE_PATH "${SourcePath}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SourcePath}/NvCloth/license.txt")
