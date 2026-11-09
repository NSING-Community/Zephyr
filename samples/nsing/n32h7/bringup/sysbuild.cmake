# Copyright (c) 2026 Nations Technologies
# SPDX-License-Identifier: Apache-2.0

# The Cortex-M4 image is built from the same application source, so this file
# is processed again for that image. Add the child only once.
if(SB_CONFIG_N32H7_CM4_IMAGE AND NOT TARGET n32h7_cm4)
  # The Cortex-M4 image lives in the independent 0x15080000 xSPI window. It
  # is not part of the default flash sequence: the validated programming flow
  # merges both HEX files and loads them through the CM7 debug port, because
  # the CM4 J-Link device script is not reliable while CM7 is running.
  ExternalZephyrProject_Add(
    APPLICATION n32h7_cm4
    SOURCE_DIR ${APP_DIR}
    BOARD n32h787xib7_stb/n32h787/cm4
    BUILD_ONLY TRUE
  )

  set(n32h7_cm4_EXTRA_CONF_FILE
    ${APP_DIR}/cm4_product.conf
    CACHE INTERNAL "Cortex-M4 image configuration fragment"
  )
endif()
