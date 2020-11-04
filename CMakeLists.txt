cmake_minimum_required(VERSION 3.9)
project(goattracker2)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(GOATTRK2_SOURCES
    src/goatdata.c
    src/goattrk2.c
    src/gsong.c
    src/gorder.c
    src/gpattern.c
    src/ginstr.c
    src/gtable.c
    src/gplay.c
    src/gdisplay.c
    src/gfile.c
    src/greloc.c
    src/ghelp.c
    src/gsound.c
    src/gconsole.c
    src/gsid.cpp
    src/resid/dac.cc
    src/resid/envelope.cc
    src/resid/extfilt.cc
    src/resid/filter.cc
    src/resid/pot.cc
    src/resid/sid.cc
    src/resid/wave.cc
    src/resid/voice.cc
    src/resid-fp/envelopefp.cpp
    src/resid-fp/extfiltfp.cpp
    src/resid-fp/filterfp.cpp
    src/resid-fp/potfp.cpp
    src/resid-fp/sidfp.cpp
    src/resid-fp/wavefp.cpp
    src/resid-fp/voicefp.cpp
    src/asm/asmtab.c
    src/asm/chnkpool.c
    src/asm/expr.c
    src/asm/lexyy.c
    src/asm/log.c
    src/asm/membuf.c
    src/asm/membufio.c
    src/asm/namedbuf.c
    src/asm/parse.c
    src/asm/pc.c
    src/asm/vec.c
    src/bme/bme_gfx.c
    src/bme/bme_snd.c
    src/bme/bme_win.c
    src/bme/bme_mou.c
    src/bme/bme_kbd.c
    src/bme/bme_io.c
    src/bme/bme_end.c
    src/bme/bme.c)

# it would be nice not having to link things like resid, however the source is
# not ready for that
set(GT2RELOC_SOURCES
    src/goatdata.c
    src/gt2reloc.c
    src/gsong.c
    src/gorder.c
    src/gpattern.c
    src/ginstr.c
    src/gtable.c
    src/gplay.c
    src/gdisplay.c
    src/gfile.c
    src/ghelp.c
    src/gsound.c
    src/gconsole.c
    src/gsid.cpp
    src/resid/dac.cc
    src/resid/envelope.cc
    src/resid/extfilt.cc
    src/resid/filter.cc
    src/resid/pot.cc
    src/resid/sid.cc
    src/resid/wave.cc
    src/resid/voice.cc
    src/resid-fp/envelopefp.cpp
    src/resid-fp/extfiltfp.cpp
    src/resid-fp/filterfp.cpp
    src/resid-fp/potfp.cpp
    src/resid-fp/sidfp.cpp
    src/resid-fp/wavefp.cpp
    src/resid-fp/voicefp.cpp
    src/asm/asmtab.c
    src/asm/chnkpool.c
    src/asm/expr.c
    src/asm/lexyy.c
    src/asm/log.c
    src/asm/membuf.c
    src/asm/membufio.c
    src/asm/namedbuf.c
    src/asm/parse.c
    src/asm/pc.c
    src/asm/vec.c
    src/bme/bme_gfx.c
    src/bme/bme_snd.c
    src/bme/bme_win.c
    src/bme/bme_mou.c
    src/bme/bme_kbd.c
    src/bme/bme_io.c
    src/bme/bme_end.c
    src/bme/bme.c)

set(MOD2SNG_SOURCES
    src/mod2sng.c
    src/bme/bme_end.c)

set(INS2SND2_SOURCES
    src/ins2snd2.c
    src/bme/bme_end.c)

set(SNGSPLI2_SOURCES
    src/sngspli2.c
    src/bme/bme_end.c)

OPTION(JACK "Use Jack audio output and midi input" OFF)
IF(JACK)
    ADD_DEFINITIONS(-DUSE_JACK)
ENDIF(JACK)

SET_SOURCE_FILES_PROPERTIES(src/resid-fp/sidfp.cpp PROPERTIES COMPILE_FLAGS "-fpermissive")

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/bme)
include_directories(${CMAKE_SOURCE_DIR}/src/asm)

add_executable(datafile src/bme/datafile.c src/bme/bme_end.c)
add_executable(dat2inc src/bme/dat2inc.c)

add_custom_command(
  OUTPUT goattrk2.dat
  COMMAND datafile goattrk2.dat goattrk2.seq
  DEPENDS datafile
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
  )

add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/src/goatdata.c
  COMMAND dat2inc goattrk2.dat goatdata.c
  DEPENDS dat2inc goattrk2.dat
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
  )

add_executable(goattrk2 ${GOATTRK2_SOURCES})
add_executable(gt2reloc ${GT2RELOC_SOURCES})
add_executable(mod2sng ${MOD2SNG_SOURCES})
add_executable(ins2snd2 ${INS2SND2_SOURCES})
add_executable(sngspli2 ${SNGSPLI2_SOURCES})

target_compile_definitions(gt2reloc PUBLIC -DGT2RELOC)

find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIR})

target_link_libraries(goattrk2 ${SDL2_LIBRARY})
target_link_libraries(gt2reloc ${SDL2_LIBRARY})
