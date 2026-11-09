# Copyright (c) 2026 Nations Technologies Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Check that the Cortex-M4 console USART receives bursts longer than its FIFO.

The USART receive FIFO is eight entries deep, so a burst is only received
without loss when the interrupt handler drains it in time.  The sample echoes
every received byte from its interrupt callback, which makes a dropped byte
visible as a corrupted line.
"""

import logging
import time

from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

# Nine bytes are the first burst that cannot fit in the receive FIFO at once.
BURST_LENGTHS = (9, 16, 24, 32, 64, 128, 200)

ECHO_READY = 'console: RX interrupt echo enabled'


def burst_payload(length: int) -> bytes:
    """Return a payload without CR, LF or command bytes of the sample."""
    return bytes(0x61 + (i % 26) for i in range(length))


def test_console_rx_burst(dut: DeviceAdapter):
    dut.readlines_until(regex=ECHO_READY)

    # The sample prints a few more banner lines after enabling the echo; the
    # Cortex-M4 main loop itself is silent.
    time.sleep(0.5)
    dut.clear_buffer()

    for length in BURST_LENGTHS:
        payload = burst_payload(length)
        logger.info('send a %d-byte burst', length)
        # The trailing newline terminates the echoed line in the test reader.
        dut.write(payload + b'\n')
        line = dut.readline(timeout=5)
        assert line == payload.decode(), f'{length}-byte burst was not echoed intact'
