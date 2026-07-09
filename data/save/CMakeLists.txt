# Save module: region file format + chunk serializer + region compactor + save manager.
# P2 task 3: save layer ported from src/core/save/.
# P2 task 4: save_manager ported (WorldData -> ChunkRegistry).
# Namespace: snt::data (was science_and_theology).

add_library(snt_data_save STATIC
    region_file.cpp
    chunk_serializer.cpp
    region_compactor.cpp
    save_manager.cpp
)
target_include_directories(snt_data_save PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_data_save PUBLIC
    snt_data_defs
    snt_data_world
    snt_engine_settings
)
