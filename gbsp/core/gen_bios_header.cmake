# Writes the open source BIOS as a C array:
#   cmake -DGBSP_BIOS_BIN=<open_gba_bios.bin> -DGBSP_BIOS_HDR=<out.h> -P gen_bios_header.cmake
# The md5 of the binary is pinned: a different BIOS changes the behaviour of games, it must not
# happen unnoticed.

if(NOT DEFINED GBSP_BIOS_BIN OR NOT DEFINED GBSP_BIOS_HDR)
  message(FATAL_ERROR "usage: cmake -DGBSP_BIOS_BIN=... -DGBSP_BIOS_HDR=... -P gen_bios_header.cmake")
endif()
file(MD5 "${GBSP_BIOS_BIN}" bios_md5)
if(NOT bios_md5 STREQUAL "1876f71b0d8c65eef3454547896ffb11")
  message(FATAL_ERROR "the BIOS binary changed: md5 ${bios_md5}")
endif()

file(READ "${GBSP_BIOS_BIN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR nbytes "${hexlen} / 2")
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " pretty "${bytes}")
file(WRITE "${GBSP_BIOS_HDR}"
"/* generated from open_gba_bios.bin (md5 ${bios_md5}, ${nbytes} bytes) */
const unsigned char open_gba_bios_rom[] = {
    ${pretty}
};
")
