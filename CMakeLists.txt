cmake_minimum_required(VERSION 3.10)

project(Rainbow C)
set(MUSASHI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/musashi)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/musashi_gen)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

file(MAKE_DIRECTORY ${GEN_DIR})

add_executable(m68kmake ${MUSASHI_DIR}/m68kmake.c)

# Tell CMake to run the m68kmake utility to generate the emulator logic
# By default, m68kmake expects the output directory and the input instruction table
add_custom_command(
    OUTPUT ${GEN_DIR}/m68kops.c ${GEN_DIR}/m68kops.h
    COMMAND m68kmake ${GEN_DIR} ${MUSASHI_DIR}/m68k_in.c
    DEPENDS m68kmake ${MUSASHI_DIR}/m68k_in.c
    COMMENT "Generating Musashi CPU core files..."
)

# Complilation
add_executable(${PROJECT_NAME}
    src/main.c
    src/memory.c
    src/screen_canvas.c
    src/tos_layer.c
    src/trap_table.c
    src/gemdos/gemdos.c
    src/gemdos/gemdos_console.c
    src/gemdos/gemdos_mem.c
    src/gemdos/gemdos_file.c
    src/gemdos/gemdos_dir.c
    src/gemdos/gemdos_find.c
    src/gemdos/gemdos_proc.c
    src/bios/bios.c
    src/bios/bios_console.c
    src/bios/bios_disk.c
    src/bios/bios_kbd.c
    src/xbios/xbios.c
    src/xbios/xbios_exec.c
    src/xbios/xbios_screen.c
    src/aes/aes.c
    src/aes/aes_pb.c
    src/aes/aes_appl.c
    src/aes/aes_graf.c
    src/aes/aes_window.c
    src/aes/aes_event.c
    src/aes/aes_menu.c
    src/aes/aes_object.c
    src/aes/aes_form.c
    src/aes/aes_rsrc.c
    src/aes/aes_shel.c
    src/vdi/vdi.c
    src/vdi/vdi_pb.c
    src/vdi/vdi_workstation.c
    src/vdi/vdi_attr.c
    src/vdi/vdi_draw.c
    src/vdi/vdi_query.c
    src/prg_loader.c
    src/guest_mem_util.c
    src/logger.c
    src/host_paths.c
    ${MUSASHI_DIR}/m68kcpu.c
    ${MUSASHI_DIR}/m68kdasm.c
    ${MUSASHI_DIR}/softfloat/softfloat.c
    ${GEN_DIR}/m68kops.c
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${MUSASHI_DIR}
    ${GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(${PROJECT_NAME} PRIVATE user32 gdi32 comdlg32)