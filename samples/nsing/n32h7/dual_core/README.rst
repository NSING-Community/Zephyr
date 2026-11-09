.. zephyr:code-sample:: n32h7-dual-core
   :name: N32H787 dual-core

   Boot the Cortex-M7 and Cortex-M4 images as two independent binaries.

Overview
********

The N32H787 is a dual-core SoC: the Cortex-M7 and Cortex-M4 run separately
linked images from two xSPI windows and are not an SMP pair. After every
system reset the Cortex-M4 is held in reset until the Cortex-M7 releases it.

This sample builds both images with sysbuild. The Cortex-M7 image prints on
USART1 and releases the Cortex-M4 image at ``0x15080000`` during
``POST_KERNEL`` initialization. The Cortex-M4 image waits for the Cortex-M7
to finish system initialization, then prints on USART3.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/nsing/n32h7/dual_core
   :board: n32h787xib7_stb/n32h787/cm7
   :goals: build
   :west-args: --sysbuild

Running
*******

The Cortex-M4 image is build-only: the two images occupy separate xSPI
windows and ``west flash`` programs only the Cortex-M7 image. Program the
Cortex-M4 binary from ``<build>/n32h7_cm4/zephyr/zephyr.bin`` at
``0x15080000`` with your debug probe before resetting the board.

CM7 console is USART1 (PA9/PA10). CM4 console is USART3 (PD8/PD9). Both run
at 115200 baud.
