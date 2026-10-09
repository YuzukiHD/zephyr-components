# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the pocketjs module is two directories up
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pocketjs_player)

set(PJS ${CMAKE_CURRENT_SOURCE_DIR}/../../pocketjs)
# -DPOCKETJS_APP=hero runs apps/hero of the PocketJS tree (480x272, buttons only), the
# default is the small app in app/
set(POCKETJS_APP "" CACHE STRING "App of the PocketJS tree to run (apps/<name>)")
if(POCKETJS_APP)
  set(APP ${PJS}/apps/${POCKETJS_APP})
  set(ROOT ${PJS})
  set(PROFILE ${CMAKE_CURRENT_SOURCE_DIR}/pocket.host.480x272.json)
else()
  set(APP ${CMAKE_CURRENT_SOURCE_DIR}/app)
  set(ROOT ${APP})
  set(PROFILE ${APP}/pocket.host.json)
endif()
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/pocketjs/player)
set(PKG ${GEN}/player.pocket)

# .pocket package of the app with the PocketJS CLI (bun); the sources it reads are
# listed in the depfile
find_package(Python3 REQUIRED)
find_program(BUN bun HINTS $ENV{HOME}/.bun/bin $ENV{HOME}/.nvm/versions/node/v24.21.0/bin REQUIRED)
add_custom_command(
  OUTPUT ${PKG}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
  COMMAND ${BUN} ${PJS}/tools/cli/bin.mjs build
    --manifest ${APP}/pocket.json --host-profile ${PROFILE}
    --project-root ${ROOT} --outdir ${GEN} --plan-dir ${GEN}/plans --output ${PKG}
    --depfile ${GEN}/player.d
  DEPFILE ${GEN}/player.d
  DEPENDS ${APP}/pocket.json ${PROFILE}
  WORKING_DIRECTORY ${PJS}
  VERBATIM)

# C header/source/assembler with the package bytes
set(EMBED ${PJS}/hosts/esp-idf/components/pocketjs_package/tools/embed_package.py)
add_custom_command(
  OUTPUT ${GEN}/pocketjs_package_player.h ${GEN}/pocketjs_package_player.c
         ${GEN}/pocketjs_package_player.S
  COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
    ${Python3_EXECUTABLE} ${EMBED} --package=${PKG} --host-profile=${PROFILE}
    --name=player --output-dir=${GEN}
  DEPENDS ${PKG} ${PROFILE}
  VERBATIM)

target_sources(app PRIVATE src/main.c src/input.c ${GEN}/pocketjs_package_player.c
  ${GEN}/pocketjs_package_player.S)
target_include_directories(app PRIVATE ${GEN})

if(CONFIG_SAMPLE_POCKETJS_PROFILE)
  target_sources(app PRIVATE src/profile.c)
endif()
