/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ncnn detects the vector unit through the auxiliary vector of Linux and
 * reports "no vector unit" everywhere else, with a vector register length of
 * 0. The layout of the tensors (the pack size) is computed from it, but the
 * vector kernels are chosen at build time, so the two disagree. This image is
 * built with the V extension, say so. libncnn.a has these two symbols of
 * cpu.cpp renamed.
 */

#include "cpu.h"

namespace ncnn {

int cpu_support_riscv_v()
{
    return 1;
}

int cpu_riscv_vlenb()
{
    int vlenb;

    asm volatile("csrr %0, vlenb" : "=r"(vlenb));
    return vlenb;
}

} // namespace ncnn
