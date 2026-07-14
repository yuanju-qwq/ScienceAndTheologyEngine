# Network replication transport module.
#
# This target contains transport-neutral replication service logic, direct
# TCP+UDP sockets, and a Steam backend adapter. It intentionally has no SDL,
# Vulkan, Godot, gameplay, or Steamworks SDK dependency.

add_library(snt_network STATIC
    replication.cpp
    replication_wire.cpp
    socket_platform.cpp
    tcp_udp_transport.cpp
    steam_p2p_transport.cpp
)
target_include_directories(snt_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(snt_network PUBLIC
    snt_engine_settings
    snt_core
)
if(WIN32)
    target_link_libraries(snt_network PUBLIC ws2_32)
endif()
