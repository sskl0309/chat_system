# Minimal hiredis config to satisfy redis++ dependency check
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h REQUIRED)
find_library(HIREDIS_LIB hiredis REQUIRED)

add_library(hiredis::hiredis UNKNOWN IMPORTED)
set_target_properties(hiredis::hiredis PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
)
