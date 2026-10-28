# LiveShell: PS5 animated wallpaper. Builds inside the OnionHEN beta2 tree as source/liveshell
# (add_subdirectory(liveshell)), using OnionHEN's libraries. GPL-3.0.
#   liveshell_shellui.elf - runs inside SceShellUI (embedded in liveshell.elf)
#   liveshell.elf         - the payload the user loads
cmake_minimum_required (VERSION 3.20)
project(liveshell C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)
set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} -fPIC -fPIE -march=znver2 -Wall -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

add_executable(liveshell_shellui ${D_CWD}/liveshell_shellui.cpp)
set_target_properties(liveshell_shellui PROPERTIES OUTPUT_NAME "liveshell_shellui.elf")
target_link_options(liveshell_shellui PRIVATE "-Wl,--gc-sections")
add_dependencies(liveshell_shellui onion_proc onion_platform onion_detour)
target_include_directories(liveshell_shellui PRIVATE
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_detour/include"
	"${PROJECT_ROOT}/include")
target_link_directories(liveshell_shellui PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries(liveshell_shellui PUBLIC onion_payload onion_proc onion_platform onion_detour SceNotification kernel_sys)
onion_strip_debug(liveshell_shellui)

add_executable(liveshell ${D_CWD}/liveshell_daemon.cpp ${D_CWD}/liveshell_web.cpp ${D_CWD}/liveshell_tile.cpp)
set_target_properties(liveshell PROPERTIES OUTPUT_NAME "liveshell.elf")
target_link_options(liveshell PRIVATE "-Wl,--gc-sections")
add_dependencies(liveshell NineS onion_proc onion_platform onion_elfldr)
set_source_files_properties(${D_CWD}/liveshell_web.cpp PROPERTIES OBJECT_DEPENDS "${D_CWD}/liveshell_page.html;${D_CWD}/liveshell_remote.html")
set_source_files_properties(${D_CWD}/liveshell_tile.cpp PROPERTIES OBJECT_DEPENDS "${D_CWD}/../assets/icon0.png")
target_compile_definitions(liveshell PRIVATE
	HYPERHEN_NO_MEDIA_TILE=1
	HYPERHEN_NATIVE_SHELLUI=1
	LIVESHELL_PAGE_HTML="${D_CWD}/liveshell_page.html"
	LIVESHELL_REMOTE_HTML="${D_CWD}/liveshell_remote.html"
	LIVESHELL_ICON_PNG="${D_CWD}/../assets/icon0.png")
target_include_directories(liveshell PRIVATE
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/include")
target_link_directories(liveshell PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries(liveshell PUBLIC NineS onion_proc onion_platform onion_elfldr SceNotification SceSystemService SceUserService kernel_sys)
onion_strip_debug(liveshell)
