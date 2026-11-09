# SPDX-License-Identifier: Apache-2.0

zephyr_sources(soc.c)
if(CONFIG_SOC_N32H787_CM4)
  # The Cortex-M4 fetches from the xSPI window at roughly 1,100-2,000 cycles
  # per instruction, so code running from there while interrupts are masked
  # stalls the 8-byte USART RX FIFO long enough to overflow it.  The vendor SDK
  # runs the whole CM4 application from AHB SRAM; do the same for the kernel,
  # the application and the interrupt paths below, and copy them at boot with
  # the code relocation mechanism.  Relocate text only: data, BSS and stacks
  # stay in the RAM region, and reset-rom-start.ld reserves the matching RX
  # MPU window for the relocated text.
  zephyr_code_relocate(
    FILES ${ZEPHYR_BASE}/drivers/timer/cortex_m_systick.c
    FILTER ".*\\.text.*"
    LOCATION SRAMCODE)

  zephyr_code_relocate(
    FILES ${ZEPHYR_BASE}/arch/arm/core/cortex_m/isr_wrapper.c
    FILTER ".*\\.text.*"
    LOCATION SRAMCODE)

  # Exception exit runs on every interrupt return, between the interrupt
  # handler and the next USART RX interrupt.
  zephyr_code_relocate(
    FILES ${ZEPHYR_BASE}/arch/arm/core/cortex_m/exc_exit.c
    FILTER ".*\\.text.*"
    LOCATION SRAMCODE)

  # The kernel runs the timeout and interrupt-exit paths; application code
  # runs the UART callbacks in interrupt context.
  zephyr_code_relocate(LIBRARY kernel LOCATION SRAMCODE FILTER ".*\\.text.*")
  zephyr_code_relocate(LIBRARY app LOCATION SRAMCODE FILTER ".*\\.text.*")

  if(CONFIG_UART_N32H7)
    # The USART driver and the standard peripheral library functions it calls
    # are the RX hot path.
    zephyr_code_relocate(
      FILES ${ZEPHYR_BASE}/drivers/serial/uart_n32h7.c
      FILTER ".*\\.text.*"
      LOCATION SRAMCODE)
    zephyr_code_relocate(
      FILES ${ZEPHYR_HAL_NSING_MODULE_DIR}/n32h78x/n32h7xx_std_periph_driver/src/n32h7xx_usart.c
      FILTER ".*\\.text.*"
      LOCATION SRAMCODE)
  endif()
endif()
zephyr_sources_ifdef(CONFIG_PINCTRL_N32H7 ${CMAKE_CURRENT_LIST_DIR}/pinctrl_soc.c)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})
if(CONFIG_SOC_N32H787_CM4)
  zephyr_compile_definitions(CORE_CM4)
  # The vendor CM4 CMSIS names SCB->SHP; Zephyr's Cortex-M code uses SHPR.
  zephyr_compile_definitions(SHPR=SHP)
else()
  zephyr_compile_definitions(CORE_CM7)
endif()
zephyr_linker_sources(ROM_START reset-rom-start.ld)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
