# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Make the symbols of simplestl.cpp.obj in libncnn.a weak: operator new and
# delete are also defined by the C++ support of the kernel, which has to win.
#
# Arguments: AR, OBJCOPY, ARCHIVE, WORKDIR

file(MAKE_DIRECTORY ${WORKDIR})
execute_process(COMMAND ${AR} x ${ARCHIVE} simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${OBJCOPY} --weaken simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${AR} r ${ARCHIVE} simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
