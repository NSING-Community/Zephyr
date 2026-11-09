# Copyright (c) 2026 Nations Technologies
# SPDX-License-Identifier: Apache-2.0

# CM7 owns the dual-core boot sequence.  Avoid a chip erase here: the CM4
# image is stored in a separate xSPI window and must survive CM7 updates.
if(CONFIG_BOARD_N32H787XIB7_STB_N32H787_CM7)
  board_runner_args(jlink "--device=N32H787xIx7:CM7" "--speed=100" "--iface=SWD" "--no-erase")
endif()

# The CM4 image lives in the independent 0x15080000 xSPI window.  Do not use
# J-Link's default chip erase here: it would erase the CM7 image as well.
# This runner is for flashing a CM4 image on a device whose CM7 image is not
# running.  While CM7 runs, program CM4 through the CM7 access port instead:
# the CM4 device script's reset and RAMCode sequence is not reliable in that
# state.
if(CONFIG_BOARD_N32H787XIB7_STB_N32H787_CM4)
  board_runner_args(jlink
    "--device=N32H787xIx7:CM4"
    "--speed=100"
    "--iface=SWD"
    "--no-erase"
    "--no-start"
  )
endif()

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
