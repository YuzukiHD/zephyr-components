# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Make symbols of libncnn.a weak, so that definitions of the adaptation layer
# win:
# - simplestl.cpp.obj: operator new and delete are also defined by the C++
#   support of the kernel
# - cpu.cpp.obj, with WEAK_CPU: the vector unit detection is renamed (a weak
#   definition would still satisfy the references and the archive member with
#   the replacement would never be pulled in), see ext/riscv_cpu.cpp
#
# Arguments: AR, OBJCOPY, ARCHIVE, WORKDIR, WEAK_CPU

file(MAKE_DIRECTORY ${WORKDIR})
execute_process(COMMAND ${AR} x ${ARCHIVE} simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${OBJCOPY} --weaken simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${AR} r ${ARCHIVE} simplestl.cpp.obj
                WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)

if(WEAK_CPU)
  execute_process(COMMAND ${AR} x ${ARCHIVE} cpu.cpp.obj
                  WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND ${OBJCOPY}
                          --redefine-sym=_ZN4ncnn19cpu_support_riscv_vEv=_ZN4ncnn19cpu_support_riscv_vEv_ncnn
                          --redefine-sym=_ZN4ncnn15cpu_riscv_vlenbEv=_ZN4ncnn15cpu_riscv_vlenbEv_ncnn
                          cpu.cpp.obj
                  WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND ${AR} r ${ARCHIVE} cpu.cpp.obj
                  WORKING_DIRECTORY ${WORKDIR} COMMAND_ERROR_IS_FATAL ANY)
endif()
