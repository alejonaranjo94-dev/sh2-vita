# sh2-vita: builds the game code from port/source with the compat layer in vita/compat,
# for the PS Vita (VitaSDK + vitaGL).
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define VITASDK (the VitaSDK install folder)")
  endif()
endif()

project(sh2vita C CXX)   # CXX: vitaGL's shader preprocessor is C++ and needs its runtime
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Silent Hill 2")
set(VITA_TITLEID "SHTW00001")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" SH2_VERSION_RAW)
string(STRIP "${SH2_VERSION_RAW}" SH2_VERSION)
set(VITA_VERSION "01.00")

set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/../port/source")

file(GLOB GAME_SOURCES "${PORT}/*.c" "${PORT}/game/view/*.c")
# 3DS-only files with a Vita replacement in vita/compat/src/replace/, or nothing to do on the Vita.
set(GAME_EXCLUDE
  profile.c      # 3DS heap, clock and core setup -> replace/profile.c
)
foreach(f ${GAME_EXCLUDE})
  list(REMOVE_ITEM GAME_SOURCES "${PORT}/${f}")
endforeach()

file(GLOB COMPAT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/compat/src/replace/*.c")

add_executable(sh2vita ${GAME_SOURCES} ${COMPAT_SOURCES})

target_include_directories(sh2vita PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat/src"
  "${PORT}"
)

target_compile_definitions(sh2vita PRIVATE __3DS__=1 SH2VITA=1 SH2_VERSION="${SH2_VERSION}")
target_compile_options(sh2vita PRIVATE
  -O2 -g -Wall -ffunction-sections -fdata-sections
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-strict-aliasing
  -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
)
target_link_options(sh2vita PRIVATE -Wl,-q -Wl,--gc-sections "-Wl,--defsym,__sce_headroom=0x2000")

target_link_libraries(sh2vita PRIVATE
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub
  SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
  SceCtrl_stub SceTouch_stub SceAudio_stub SceSysmodule_stub SceCommonDialog_stub
  SceAppMgr_stub SceAppUtil_stub SceIme_stub SceRtc_stub ScePower_stub SceSysmem_stub
  SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub
  stdc++ z m c
)
set_target_properties(sh2vita PROPERTIES LINKER_LANGUAGE CXX)

# ATTRIBUTE2=12 asks the system for the extended memory mode (more RAM for the game).
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
vita_create_self(eboot.bin sh2vita UNSAFE)

file(GLOB SCE_SYS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys" "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/*.png"
     "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/*")
set(VPK_FILES "")
foreach(f ${SCE_SYS})
  list(APPEND VPK_FILES FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/${f}" "sce_sys/${f}")
endforeach()
vita_create_vpk(sh2vita.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${VPK_FILES}
)
