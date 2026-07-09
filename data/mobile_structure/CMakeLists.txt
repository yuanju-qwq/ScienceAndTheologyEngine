# Mobile structure module: dynamic structures (ships, contraptions).
# P2 task 4: ported from src/core/mobile_structure/.
# Namespace: snt::data (was science_and_theology + mobile_structure sub-ns).
#
# The original mobile_structure sub-namespace is flattened into snt::data
# to match the P2 namespace migration decision (gt sub-ns merge precedent).

add_library(snt_data_mobile STATIC
    dynamic_structure.cpp
    ship_structure.cpp
    ship_local_grid.cpp
)
target_include_directories(snt_data_mobile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_data_mobile PUBLIC snt_data_defs snt_engine_settings)
