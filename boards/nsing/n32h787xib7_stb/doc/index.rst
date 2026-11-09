.. _n32h787xib7_stb:

NATIONS N32H787XIB7-STB
#######################

Overview
********

The N32H787XIB7-STB board is a dual-core evaluation platform for the
Nations N32H787 Cortex-M7/M4 MCU. The ``n32h787xib7_stb/n32h787/cm7``
build target provides support for the Cortex-M7 core; the
``n32h787xib7_stb/n32h787/cm4`` build target provides support for the
Cortex-M4 core of the same SoC.

Hardware
********

* N32H787 dual-core MCU
* Cortex-M7 and Cortex-M4 cores
* On-chip flash and SRAM
* SWD/J-Link debug interface
* USB-to-serial interface connected to USART1
* User GPIO/LED header

For more information about the N32H78x SoC and evaluation board, refer to
the N32H7xx SDK and the N32H7xx User Manual supplied with the board.

Supported Features
==================

The Zephyr ``n32h787xib7_stb`` board configuration supports the following
hardware features:

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| NVIC      | on-chip    | nested vector interrupt controller  |
+-----------+------------+-------------------------------------+
| UART      | USART1     | serial port-polling                 |
+-----------+------------+-------------------------------------+
| PINCTRL   | on-chip    | N32H7 AFIO/GPIO pin control         |
+-----------+------------+-------------------------------------+
| GPIO      | GPIOA      | gpio                                |
+-----------+------------+-------------------------------------+
| CLOCK     | RCC        | reset and clock control             |
+-----------+------------+-------------------------------------+
| FLASH     | on-chip    | flash memory                        |
+-----------+------------+-------------------------------------+
| SRAM      | on-chip    | SRAM memory region                  |
+-----------+------------+-------------------------------------+
| WATCHDOG  | IWDG1      | independent watchdog                |
+-----------+------------+-------------------------------------+
| WATCHDOG  | WWDG1      | window watchdog                     |
+-----------+------------+-------------------------------------+

The Cortex-M4 cluster provides the corresponding USART3, GPIOA, IWDG2 and
WWDG2 nodes. Other H78x peripherals remain disabled until their Zephyr
drivers and board pin mappings are implemented.

The default configurations can be found in:

* ``boards/nsing/n32h787xib7_stb/n32h787xib7_stb_n32h787_cm7_defconfig``
* ``boards/nsing/n32h787xib7_stb/n32h787xib7_stb_n32h787_cm4_defconfig``

Default Zephyr Peripheral Mapping
----------------------------------

* USART1 TX/RX: PA9/PA10 (CM7 console, USB-to-serial connector)
* USART3 TX/RX: PD8/PD9 (CM4 console, external adapter)

The on-board USB-to-serial connector is wired to the CM7 USART1 pins.
USART2 is available on PD5/PD6 for external wiring, which the UART driver
tests use as the cross-over peer. The CM4 console uses USART3 on PD8/PD9 and
needs an external adapter.

The board DTS explicitly selects the UART pinctrl state and baud rate:

.. code-block:: devicetree

   &usart1 {
       pinctrl-0 = <&usart1_tx &usart1_rx>;
       pinctrl-names = "default";
       current-speed = <115200>;
       status = "okay";
   };

System Clock
------------

The validated reset clock is the internal HSI at 64 MHz. The H78X SoC DTS
therefore declares ``clock-frequency = <64000000>`` for the RCC provider.

Flashing
========

.. code-block:: console

   west build -b n32h787xib7_stb/n32h787/cm7 samples/hello_world
   west flash --runner jlink

The CM7 runner uses J-Link SWD with the ``N32H787xIx7:CM7`` device name.
The CM4 image is built separately:

.. code-block:: console

   west build -b n32h787xib7_stb/n32h787/cm4 samples/hello_world

The Cortex-M7 image is linked at ``0x15000000`` and the Cortex-M4 image at
``0x15080000``; each image has a 512 KiB flash window.  Flash the Cortex-M7
image normally, then program the
Cortex-M4 image through the Cortex-M7 access port with J-Link Commander:

.. code-block:: console

   JLinkExe -device N32H787xIx7:CM7 -if SWD -speed 100
   J-Link> r
   J-Link> loadbin build-cm4/zephyr/zephyr.bin 0x15080000
   J-Link> r
   J-Link> g

Do not select the Cortex-M4 J-Link device (``N32H787xIx7:CM4``) while the
Cortex-M4 is held in reset: the probe cannot power up its debug port in that
state.  Once a Cortex-M7 image has released the Cortex-M4, the runner can
program and start a Cortex-M4 image.  Do not mass erase the device when
updating Cortex-M4 alone, because an erase of the Cortex-M7 flash region also
removes the Cortex-M4 image.

After any system reset the Cortex-M4 is held in reset again.  The
Cortex-M7 image must release it with the SDK-compatible sequence::

   RCC_EnableCM4(0x15080000);

The Cortex-M7 console on USART1 is only needed for the Cortex-M7 image's
own output; observe the Cortex-M4 image on USART3 (PD8/PD9) instead.

Testing
=======

Tests can run on hardware through Twister's device testing.  Describe the
board and the serial adapter attached to the console of the core under test
in a hardware map:

.. code-block:: yaml

   - platform: n32h787xib7_stb/n32h787/cm4
     id: "59700618"
     product: J-Link
     runner: jlink
     connected: true
     serial: /dev/ttyUSB0

Then run:

.. code-block:: console

   west twister --device-testing --hardware-map n32h787.yaml \
     -p n32h787xib7_stb/n32h787/cm4 \
     -T tests/drivers/uart/nsing -T samples/nsing/n32h7/bringup \
     --west-flash="--device=N32H787xIx7:CM7,--start"

The Cortex-M4 console is USART3 on PD8/PD9, so ``serial`` names the adapter
connected to those pins.  Twister programs the Cortex-M4 image with the
board's J-Link runner, which needs a Cortex-M7 image in flash that releases
the Cortex-M4, for example the bring-up sample built with
``cm7_product.conf``.  Flash that image first; a test that drives USART1 or
USART2 from the Cortex-M4 needs the quiet companion described below.

The Cortex-M4 image is linked in its independent ``0x15080000`` xSPI window.
The carrier board runner cannot start it while the Cortex-M4 is held in
reset, so program the image through the Cortex-M7 access port and start it
with the Cortex-M7 by overriding the runner device:

.. code-block:: console

   west twister --device-testing --hardware-map n32h787.yaml \
     -p n32h787xib7_stb/n32h787/cm4 \
     -T tests/drivers/uart/nsing \
     --west-flash="--device=N32H787xIx7:CM7,--start"

``tests/drivers/uart/nsing`` drives USART1 (PA9/PA10) and USART2 (PD5/PD6)
through external cross-over wiring: connect PA9 to PD6 and PD5 to PA10.  The
suite skips the loopback cases when the wiring is not installed.

Both cores share the GPIO and pin control registers, so the Cortex-M7 image
releasing the Cortex-M4 must not use USART1 or USART2 while the Cortex-M4 test
drives them.  Build that companion image with ``cm7_quiet.conf``, which keeps
the Cortex-M7 console and its RX echo interrupt disabled:

.. code-block:: console

   west build -b n32h787xib7_stb/n32h787/cm7 samples/nsing/n32h7/bringup \
     -d build-cm7-quiet \
     -- -DEXTRA_CONF_FILE="uart.conf;cm7_product.conf;cm7_quiet.conf"
   west flash -d build-cm7-quiet

``sample.nsing.n32h7.bringup.cm4.rx_burst`` sends bursts of 9 to 200 bytes
to the console and checks that every byte is echoed, covering the receive
FIFO overflow seen when the Cortex-M4 executes from xSPI flash.

Debugging
=========

Use the J-Link runner or connect directly with:

.. code-block:: console

   JLinkExe -device N32H787xIx7:CM7 -if SWD -speed 100
