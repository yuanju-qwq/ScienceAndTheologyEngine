# P6 Unicode text dependency bridge: expose the vendored static zlib target
# to FreeType's find_package(ZLIB) call.
if(TARGET snt_zlib)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIRS "${_SNT_ZLIB_DIR}")
    set(ZLIB_LIBRARIES snt_zlib)
else()
    set(ZLIB_FOUND FALSE)
endif()