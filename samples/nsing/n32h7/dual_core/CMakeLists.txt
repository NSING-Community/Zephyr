# Copyright (c) 2026 Nations Technologies
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(n32h7_dual_core)

target_sources(app PRIVATE src/main.c)
