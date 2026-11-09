# N32H787 hardware-validation and dual-core product-baseline sample

This sample is the board-level validation image for the N32H787 CM7 port. It
is intentionally separate from product applications. It exercises the porting
baseline: boot/VTOR, RCC status reporting, USART console, GPIOA outputs
(PA2/PA3/PA4), watchdog integration, and the dual-core boot gate.

The common build uses two configuration fragments:

```text
prj.conf;uart.conf
```

This enables the console, GPIO test and IWDG feed path. Window watchdog
coverage lives in `tests/drivers/watchdog/wdt_basic_api`; the sample only
keeps the IWDG reset test as an opt-in fragment:

* `iwdg_reset_test.conf` — stops feeding after six cycles and verifies the
  `RCC_CTRLSTS_IWDG1RSTF` reset flag.

For an IWDG reset test, always keep a recovery image (for example
`cm7_recovery.conf`) available for recovery. A successful test prints a new
Zephyr boot banner and `RCC_CTRLSTS` with bit 5 (`IWDG1RSTF`) set.

The sample application is not intended to be merged into a production image;
use the product fragments as a reference and keep the driver/Devicetree
changes in the board port.

# N32H78X dual-core product validation

The `cm7_product.conf` and `cm4_product.conf` fragments are the conservative
product baseline.  They keep the validated CM7-first boot gate and initialize
DCMU, but do not enable the diagnostic DCMU exchange or install a second CM4
mailbox consumer on channel 0.

Build both clusters in one sysbuild invocation:

```sh
west build -b n32h787xib7_stb/n32h787/cm7 samples/nsing/n32h7/bringup \
  --sysbuild -d build-n32h7-product \
  -- '-Dbringup_EXTRA_CONF_FILE=uart.conf;cm7_product.conf'
```

The Cortex-M7 image is the main image; `sysbuild.cmake` adds the Cortex-M4
image from the same application source with `cm4_product.conf`. The CM4 image
is build-only and is not part of the default flash sequence.

The two images can also be built separately:

```sh
west build -b n32h787xib7_stb/n32h787/cm7 samples/nsing/n32h7/bringup \
  -d build-n32h7-cm7-product -- '-DEXTRA_CONF_FILE=uart.conf;cm7_product.conf'
west build -b n32h787xib7_stb/n32h787/cm4 samples/nsing/n32h7/bringup \
  -d build-n32h7-cm4-product -- -DEXTRA_CONF_FILE=cm4_product.conf
```

Do not flash or attach through the CM4 J-Link device script: its system-reset
register accesses and RAMCode prepare sequence are not reliable while the
dual-core image is running. Merge the two HEX files and program both xSPI
partitions through the CM7 AP instead. The validated flow is: build both
images, merge `build-n32h7-product/bringup/zephyr/zephyr.hex` and
`build-n32h7-product/n32h7_cm4/zephyr/zephyr.hex` with
`scripts/build/mergehex.py`, power-cycle the board, and load the merged image
through the CM7 AP.

Acceptance criteria are:

* CM7 reports `N32H7 CM7 bring-up` and `shared reset` without a fault;
* CM4 reaches `CM4_STATE_RUNNING` and its shared heartbeat increases; and
* the CM7 console remains alive while the CM4 heartbeat continues increasing.

The USB serial connector on the reference board is CM7 USART1. The CM4
console is USART3 on PD8 (TX) and PD9 (RX), so it needs an external adapter
wired to those pins; the CM4 image prints there only when such an adapter is
connected. Output, the boot banner and interrupt-driven RX on USART3 have been
validated on the target that way. USART2 is available on PD5/PD6 and is used by
`tests/drivers/uart/nsing` as the cross-over peer of USART1.

For device testing a CM4 image that drives USART1 and USART2, build the CM7
companion with `cm7_quiet.conf` in addition to `cm7_product.conf`. Both
clusters share the GPIO and pin control registers, so the companion must not
initialize the USART1 console or enable its RX echo interrupt while the CM4
uses those pins:

```sh
west build -b n32h787xib7_stb/n32h787/cm7 samples/nsing/n32h7/bringup \
  -d build-cm7-quiet -- '-DEXTRA_CONF_FILE=uart.conf;cm7_product.conf;cm7_quiet.conf'
west flash -d build-cm7-quiet
```

## Reset scope verified against the Nations SDK

The local Nations N32H7xx SDK and 60730 Class-B dual-core startup code expose
`RCC_EnableCM4()`/`RCC->M4RSTREL` for CM4 release, but do not provide a
CM4-only reset/deassert API. The Class-B watchdog self-test explicitly models
the CM4 IWDG2 test as a **system reset**, and WWDG2 cannot reset the CM4 core
alone either; both watchdogs reset the whole SoC. `CM4SFTRST`, `IWDG2RST` and
`WWDG2RST` are retained as reset-cause flags. Therefore the current Zephyr port treats CM4
reset recovery as system-level recovery; it must not clear `M4RSTREL_EN` or
invent a private CM4-only reset sequence without a vendor register update.
