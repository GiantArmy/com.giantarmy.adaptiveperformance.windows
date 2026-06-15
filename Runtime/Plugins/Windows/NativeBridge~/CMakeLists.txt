cmake_minimum_required(VERSION 3.21)

project(GiantArmyWindowsThermalBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(GiantArmyWindowsThermalBridge SHARED
    GiantArmyWindowsThermalBridge.cpp
    ThermalNvidia.cpp
    ThermalAmd.cpp
    ThermalIntel.cpp
    ThermalFallbacks.cpp
)

target_compile_definitions(GiantArmyWindowsThermalBridge PRIVATE
    GAWT_BRIDGE_EXPORTS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(GiantArmyWindowsThermalBridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(GiantArmyWindowsThermalBridge PRIVATE
    wbemuuid
    ole32
    oleaut32
    pdh
)

set_target_properties(GiantArmyWindowsThermalBridge PROPERTIES
    OUTPUT_NAME "GiantArmyWindowsThermalBridge"
)

# Copy built DLL to the Unity plugin directory (Runtime/Plugins/Windows/x86_64/)
set(UNITY_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../x86_64")
add_custom_command(TARGET GiantArmyWindowsThermalBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:GiantArmyWindowsThermalBridge>"
        "${UNITY_PLUGIN_DIR}/GiantArmyWindowsThermalBridge.dll"
    COMMENT "Copying GiantArmyWindowsThermalBridge.dll to Runtime/Plugins/Windows/x86_64/"
)

add_executable(ThermalBridgeCli
    tools/ThermalBridgeCli.cpp
)

target_include_directories(ThermalBridgeCli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ThermalBridgeCli PRIVATE
    GiantArmyWindowsThermalBridge
)
