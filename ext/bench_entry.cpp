/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* the main() of benchncnn is built under another name, C++ linkage */
extern int ncnn_bench_main_cpp(int argc, char** argv);

extern "C" int ncnn_bench_main(int argc, char** argv)
{
    return ncnn_bench_main_cpp(argc, argv);
}
