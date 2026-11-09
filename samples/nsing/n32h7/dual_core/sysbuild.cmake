# Copyright (c) 2026 Nations Technologies
# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_N32H7_CM4_IMAGE AND NOT TARGET n32h7_cm4)
  ExternalZephyrProject_Add(
    APPLICATION n32h7_cm4
    SOURCE_DIR ${APP_DIR}/cm4
    BOARD n32h787xib7_stb/n32h787/cm4
    BUILD_ONLY TRUE
  )
endif()
