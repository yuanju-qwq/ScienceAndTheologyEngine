# P6 Unicode text dependency bridge: expose the vendored static libpng target
# to FreeType's find_package(PNG) call.
if(TARGET snt_libpng)
    set(PNG_FOUND TRUE)
    set(PNG_PNG_INCLUDE_DIR "${_SNT_LIBPNG_DIR}")
    set(PNG_INCLUDE_DIRS "${_SNT_LIBPNG_DIR}")
    set(PNG_LIBRARY snt_libpng)
    set(PNG_LIBRARIES snt_libpng)
else()
    set(PNG_FOUND FALSE)
endif()