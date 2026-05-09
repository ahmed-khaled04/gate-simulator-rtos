# Button → GPIO Map (Keil Simulator)

## How to toggle a button
Peripherals → GPIO → GPIO [Port] → DATA register → set or clear the bit

---

## Panel Buttons (press = hold, release to trigger MANUAL or AUTO)

| Button | Port | Bit | Active | Press | Release |
|--------|------|-----|--------|-------|---------|
| Driver OPEN | GPIO F | 4 | LOW | Clear bit 4 → 0 | Set bit 4 → 1 |
| Driver CLOSE | GPIO F | 0 | LOW | Clear bit 0 → 0 | Set bit 0 → 1 |
| Security OPEN | GPIO E | 1 | HIGH | Set bit 1 → 1 | Clear bit 1 → 0 |
| Security CLOSE | GPIO B | 0 | HIGH | Set bit 0 → 1 | Clear bit 0 → 0 |

> Driver OPEN (PF4) and Driver CLOSE (PF0) are both active-LOW (pull-up) — resting state is 1, pressed state is 0.

---

## Limit & Safety Buttons (pulse only — press then release)

| Button | Port | Bit | Active | Press | Release |
|--------|------|-----|--------|-------|---------|
| Open Limit | GPIO B | 1 | HIGH | Set bit 1 → 1 | Clear bit 1 → 0 |
| Closed Limit | GPIO D | 0 | HIGH | Set bit 0 → 1 | Clear bit 0 → 0 |
| Obstacle | GPIO D | 1 | HIGH | Set bit 1 → 1 | Clear bit 1 → 0 |

---

## LED Indicators (read-only — GPIO F output)

| LED | Bit | Meaning |
|-----|-----|---------|
| RED | PF1 | Gate CLOSING |
| BLUE | PF2 | STOPPED_MIDWAY (solid) / REVERSING (blink) |
| GREEN | PF3 | Gate OPENING |

---

## Quick Test Sequences

**Get to IDLE_OPEN:**
1. GPIO F bit 4 → 0 (DRV_OPEN press) — GREEN on
2. GPIO B bit 1 → 1 (Open Limit) — GREEN off → IDLE_OPEN

**Get to IDLE_CLOSED:**
1. GPIO F bit 0 → 0 (DRV_CLOSE press, active-low) — RED on
2. GPIO D bit 0 → 1 (Closed Limit) — RED off → IDLE_CLOSED

**Trigger Obstacle (must be during CLOSING):**
1. GPIO D bit 1 → 1 (Obstacle press)
2. Watch: RED off → GREEN + BLUE blink 500ms → BLUE solid

**Conflict (same panel OPEN + CLOSE together):**
- Driver: GPIO F bit 4 → 0 AND GPIO E bit 0 → 1 simultaneously
- Security: GPIO E bit 1 → 1 AND GPIO B bit 0 → 1 simultaneously
