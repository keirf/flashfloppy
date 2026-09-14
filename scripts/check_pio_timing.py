# check_pio_timing.py
#
# Static verification of the RP2350 PIO flux-engine timing
# (src/pico2/floppy.c). Extracts the PIO program and the FLUX_LEAD
# constant from the source, decodes the instructions, and checks:
#
#  1. The RDATA generator's period is exactly (FLUX_LEAD + X) SAMPLECLK
#     ticks for an interval value X (2 PIO cycles per tick at 144MHz),
#     so that flux_adjust()'s rebias reproduces the STM32 (ARR+1) ticks.
#  2. The RDATA pulse width is within the floppy-interface spec.
#  3. The WDATA capture loops sample at exactly 2 cycles per tick in
#     both pin states.
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import re, sys, os

SYSCLK_MHZ = 144
CYCLES_PER_TICK = 2  # 72MHz SAMPLECLK

def die(msg):
    print("FAIL: " + msg)
    sys.exit(1)

def delay(op):
    return (op >> 8) & 0x1f  # no side-set configured: 5 delay bits

def main():
    src = os.path.join(os.path.dirname(__file__), "..", "src/pico2/floppy.c")
    with open(src) as f:
        text = f.read()

    prog = [int(m, 16) for m in re.findall(
        r"/\*\s*\d+\s*\*/\s*0x([0-9a-fA-F]{4}),", text)]
    if len(prog) != 16:
        die("expected 16 program words, found %d" % len(prog))

    flux_lead = int(re.search(r"#define FLUX_LEAD (\d+)", text).group(1))

    # --- RDATA generator (instructions 0-4, wrap 4->0) ---
    # out x,16 ; set pins,0 [d1] ; nop [d2] ; set pins,1 ; jmp x-- [d4]
    # (push-pull: assert = drive low, deassert = drive high)
    if prog[0] != 0x6030:
        die("instr 0 is not 'out x, 16'")
    if (prog[1] & 0xe0ff) != 0xe000:
        die("instr 1 is not 'set pins, 0'")
    if (prog[3] & 0xe0ff) != 0xe001:
        die("instr 3 is not 'set pins, 1'")
    if (prog[4] & 0xe0ff) != 0x0044:
        die("instr 4 is not 'jmp x-- 4'")

    pulse_cycles = (1 + delay(prog[1])) + (1 + delay(prog[2]))
    fixed_cycles = 1 + pulse_cycles + 1  # out, pulse+gap, set-low
    loop_cycles = 1 + delay(prog[4])

    # Period for interval value X: fixed + loop*(X+1) cycles.
    if loop_cycles != CYCLES_PER_TICK:
        die("countdown loop is %d cycles/tick, need %d"
            % (loop_cycles, CYCLES_PER_TICK))

    for x in (0, 100, 1000, 65535):
        period_cycles = fixed_cycles + loop_cycles * (x + 1)
        period_ticks = period_cycles / CYCLES_PER_TICK
        if period_ticks != flux_lead + x:
            die("period(X=%d) = %s ticks, expected %d"
                % (x, period_ticks, flux_lead + x))

    # flux_adjust: STM32 entry v plays (v+1) ticks; adjusted entry
    # (v - (FLUX_LEAD-1)) plays (v - FLUX_LEAD + 1 + FLUX_LEAD) = v+1. OK
    # by construction; verify the source uses the same constant.
    if not re.search(r"x - \(FLUX_LEAD-1\)", text):
        die("flux_adjust does not rebias by FLUX_LEAD-1")

    pulse_ns = pulse_cycles * 1000 // SYSCLK_MHZ
    if not (150 <= pulse_ns <= 800):
        die("RDATA pulse width %dns outside 150-800ns" % pulse_ns)

    # --- WDATA capture (instructions 8-15) ---
    expect = {8: 0xa02b, 9: 0x004a, 10: 0x00c9, 11: 0xa0c9,
              12: 0x8000, 13: 0x00c9, 14: 0x004d, 15: 0x000d}
    for i, op in expect.items():
        if prog[i] != op:
            die("instr %d is %04x, expected %04x" % (i, prog[i], op))
    # Both count loops (9+10 and 13+14) are 2 instructions, no delays:
    for i in (9, 10, 13, 14):
        if delay(prog[i]) != 0:
            die("wdata loop instr %d has unexpected delay" % i)

    print("OK: RDATA period = (%d+X) ticks; pulse %dns; "
          "WDATA sampling 2 cycles/tick." % (flux_lead, pulse_ns))
    return 0

if __name__ == "__main__":
    sys.exit(main())
