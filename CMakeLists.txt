cmake_minimum_required(VERSION 3.14)
project(EUI-NEO VERSION 1.0)

# ─────────────────────────────────────────────────────────────────────────────
# 渲染后端选择
#   EUI_D3D11=ON  → Direct3D 11（仅 Windows，默认）
#                   cmake -B build-d3d11 -DEUI_D3D11=ON
#
#   EUI_D3D11=OFF → OpenGL 3.3 Core Profile + glad（跨平台）
#                   cmake -B build-opengl -DEUI_D3D11=OFF
#
# 源码中通过 #ifdef EUI_D3D11 区分两条路径，两者可同时存在于不同构建目录。
# ─────────────────────────────────────────────────────────────────────────────
option(EUI_D3D11 "Use Direct3D 11 renderer instead of OpenGL (Windows only)" ON)

if(APPLE)
    enable_language(OBJC)
    set_source_files_properties(core/tray_bridge.c PROPERTIES LANGUAGE OBJC)
    set_source_files_properties(core/ime_bridge.c PROPERTIES LANGUAGE OBJC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

# 依赖获取模式：
#   bundled → 使用 3rd/ 目录下的 vendored 源码（离线，快）
#   auto    → 优先使用 3rd/，若不存在则自动 FetchContent 下载（推荐默认）
#   fetch   → 总是从网络下载，忽略 3rd/
set(EUI_DEPS_MODE "auto" CACHE STRING "Third-party dependency mode: bundled, auto, or fetch")
set_property(CACHE EUI_DEPS_MODE PROPERTY STRINGS bundled auto fetch)
if(NOT EUI_DEPS_MODE MATCHES "^(bundled|auto|fetch)$")
    message(FATAL_ERROR "EUI_DEPS_MODE must be one of: bundled, auto, fetch")
endif()

set(EUI_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rd")

function(eui_use_bundled_dependency out_var dep_name source_dir marker_file)
    if(EUI_DEPS_MODE STREQUAL "fetch")
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${source_dir}/${marker_file}")
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()

    if(EUI_DEPS_MODE STREQUAL "auto")
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "Bundled dependency '${dep_name}' is missing at ${source_dir}. "
        "Restore the vendored source, or configure with -DEUI_DEPS_MODE=auto/fetch to allow network fetches."
    )
endfunction()

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)

include(FetchContent)

eui_use_bundled_dependency(
    EUI_USE_BUNDLED_GLFW
    "GLFW"
    "${EUI_THIRD_PARTY_DIR}/glfw"
    "CMakeLists.txt"
)

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
if(EUI_USE_BUNDLED_GLFW)
    add_subdirectory("${EUI_THIRD_PARTY_DIR}/glfw" "${CMAKE_CURRENT_BINARY_DIR}/_deps/glfw-bundled-build" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
        URL_HASH SHA256=a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TIMEOUT 30
    )
    FetchContent_MakeAvailable(glfw)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
eui_use_bundled_dependency(
    EUI_USE_BUNDLED_GLAD
    "glad"
    "${EUI_THIRD_PARTY_DIR}/glad"
    "src/glad.c"
)

if(EUI_USE_BUNDLED_GLAD)
    set(glad_SOURCE_DIR "${EUI_THIRD_PARTY_DIR}/glad")
else()
    FetchContent_Declare(
        glad
        URL https://github.com/libigl/libigl-glad/archive/651a425101365aa6e8504988ef9bb363d066c5ee.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TIMEOUT 30
    )
    FetchContent_GetProperties(glad)
    if(NOT glad_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(glad)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
endif()
add_library(glad "${glad_SOURCE_DIR}/src/glad.c")
target_include_directories(glad PUBLIC "${glad_SOURCE_DIR}/include")
if(NOT WIN32)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

eui_use_bundled_dependency(
    EUI_USE_BUNDLED_TRAY
    "tray"
    "${EUI_THIRD_PARTY_DIR}/tray"
    "tray.h"
)

if(EUI_USE_BUNDLED_TRAY)
    set(TRAY_INCLUDE_DIR "${EUI_THIRD_PARTY_DIR}/tray")
else()
    FetchContent_Declare(
        tray
        URL https://github.com/zserge/tray/archive/8dd1358b92562faf7c032cf5362fa97cbc7e13e9.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TIMEOUT 30
    )
    FetchContent_MakeAvailable(tray)
    set(TRAY_INCLUDE_DIR "${tray_SOURCE_DIR}")
endif()

# OpenGL 仅在非 D3D11 模式下需要
if(NOT EUI_D3D11)
    find_package(OpenGL REQUIRED)
endif()
if(NOT WIN32)
    find_package(CURL REQUIRED)
else()
    find_package(CURL QUIET)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(EUI_APPINDICATOR QUIET appindicator3-0.1)
        pkg_check_modules(EUI_GTK3 QUIET gtk+-3.0)
    endif()
endif()

set(CORE_SOURCES
    core/async.cpp
    core/image.cpp
    core/ime_bridge.c
    core/network.cpp
    core/platform.cpp
    core/tray_bridge.c
    core/text.cpp
)

set(APP_ASSET_FILES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    file(GLOB APP_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
endif()

set(APP_ICON_RESOURCE)
if(WIN32)
    set(APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/eui_app_icon.rc")
    file(TO_NATIVE_PATH "${APP_ICON_ICO}" APP_ICON_ICO_NATIVE)
    string(REPLACE "\\" "\\\\" APP_ICON_ICO_ESCAPED "${APP_ICON_ICO_NATIVE}")
    file(WRITE "${APP_ICON_RC}" "IDI_APP_ICON ICON \"${APP_ICON_ICO_ESCAPED}\"\n")
    set(APP_ICON_RESOURCE "${APP_ICON_RC}")
endif()

function(configure_opengl_app target_name)
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TRAY_INCLUDE_DIR})

    if(EUI_D3D11)
        # D3D11 路径：注入宏 EUI_D3D11=1，链接系统 D3D11/DXGI/D3DCompiler
        # glad / OpenGL 不参与链接
        target_compile_definitions(${target_name} PRIVATE EUI_D3D11=1)
        target_link_libraries(${target_name} PRIVATE glfw d3d11 dxgi d3dcompiler)
    else()
        # OpenGL 路径：链接 glad（GL 加载器）+ OpenGL
        target_link_libraries(${target_name} PRIVATE glfw glad OpenGL::GL)
    endif()

    if(MSVC)
        set_target_properties(${target_name} PROPERTIES VS_GLOBAL_VcpkgXUseBuiltInApplocalDeps true)
        target_compile_options(${target_name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GS- /sdl- /utf-8)
        target_link_options(${target_name} PRIVATE /OPT:REF /OPT:ICF /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS)
    elseif(APPLE)
        target_compile_options(${target_name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Os -fno-exceptions -fno-rtti>)
        target_link_options(${target_name} PRIVATE -Wl,-dead_strip)
    else()
        target_compile_options(${target_name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Os -fno-exceptions -fno-rtti>)
        target_link_options(${target_name} PRIVATE -Wl,--gc-sections -s)
    endif()

    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE EUI_TRAY_WINAPI=1)
        target_link_libraries(${target_name} PRIVATE winmm urlmon shell32 user32 imm32)

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:glfw>
            $<TARGET_FILE_DIR:${target_name}>
        )
    elseif(APPLE)
        target_compile_definitions(${target_name} PRIVATE EUI_TRAY_APPKIT=1)
        target_link_libraries(${target_name} PRIVATE "-framework Cocoa" objc)
    elseif(UNIX AND EUI_APPINDICATOR_FOUND AND EUI_GTK3_FOUND)
        target_compile_definitions(${target_name} PRIVATE EUI_TRAY_APPINDICATOR=1)
        target_include_directories(${target_name} PRIVATE ${EUI_APPINDICATOR_INCLUDE_DIRS} ${EUI_GTK3_INCLUDE_DIRS})
        target_compile_options(${target_name} PRIVATE ${EUI_APPINDICATOR_CFLAGS_OTHER} ${EUI_GTK3_CFLAGS_OTHER})
        target_link_directories(${target_name} PRIVATE ${EUI_APPINDICATOR_LIBRARY_DIRS} ${EUI_GTK3_LIBRARY_DIRS})
        target_link_libraries(${target_name} PRIVATE ${EUI_APPINDICATOR_LIBRARIES} ${EUI_GTK3_LIBRARIES})
    endif()

    if(TARGET CURL::libcurl)
        target_link_libraries(${target_name} PRIVATE CURL::libcurl)
        target_compile_definitions(${target_name} PRIVATE EUI_HAS_CURL=1)
    endif()

    if(APP_ASSET_FILES)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>/assets
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${APP_ASSET_FILES} $<TARGET_FILE_DIR:${target_name}>/assets
        )
    endif()
endfunction()

file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp")
foreach(app_source ${APP_SOURCES})
    get_filename_component(app_name "${app_source}" NAME_WE)

    # 纯 Win32 Demo：不链接框架 main.cpp，app 源文件包含完整的 main() 入口
    # 这类 demo 名称以 "demo_win32" 开头
    if(app_name MATCHES "^demo_win32")
        if(NOT EUI_D3D11)
            # Win32 Demo 只支持 D3D11 模式
            message(STATUS "Skipping ${app_name}: requires EUI_D3D11=ON")
            continue()
        endif()
        add_executable(${app_name} "${app_source}" ${CORE_SOURCES} ${APP_ICON_RESOURCE})
        # 继承标准配置（D3D11 链接、Windows 库等），但注意 ENTRY 点是 main
        configure_opengl_app(${app_name})
        # Win32 Demo 需要 imm32 用于 IME 支持
        target_link_libraries(${app_name} PRIVATE imm32)
    else()
        # 标准 App：链接框架 main.cpp
        add_executable(${app_name} main.cpp "${app_source}" ${CORE_SOURCES} ${APP_ICON_RESOURCE})
        configure_opengl_app(${app_name})
    endif()
endforeach()
