# ESP-NOW RC Car

A radio-controlled 4WD car built on **Arduino Nano ESP32** boards. A joystick
remote talks to the car over **ESP-NOW** (peer-to-peer 2.4 GHz, no WiFi router),
and the car drives an **L293D motor shield** with all four wheels using
tank-style (differential) steering. Includes a triple-click **demo show**.

```
 REMOTE  (Arduino Nano ESP32)                 CAR  (Arduino Nano ESP32)
 ┌───────────────────────┐                    ┌────────────────────────────┐
 │ KY-023 joystick        │                    │ L293D motor shield (4 DC)   │
 │ remote_espnow.ino      │── ESP-NOW ────────▶│ car_l293d.ino               │
 │  reads X/Y/button,     │   broadcast        │  receives, tank-mixes,      │
 │  broadcasts ~50x/s     │                    │  drives M1..M4              │
 └───────────────────────┘                    │  + triple-click demo show   │
                                               │ 3S LiPo (11.1V)             │
                                               └────────────────────────────┘
```

---

## Hardware

| Qty | Part | Role |
|-----|------|------|
| 2 | **Arduino Nano ESP32** (ESP32-S3) | remote + car brains |
| 1 | 4WD acrylic chassis + 4× TT gear motors | drivetrain |
| 1 | **L293D motor shield** (MH-Electronics / Adafruit v1 clone) | motor driver |
| 1 | KY-023 analog joystick | remote input |
| 1 | 3S LiPo (~11.1–12.6V) | motor power |
| — | jumper wires | wiring |
| 1 | *(for cordless)* 5V buck converter / UBEC | shield logic power off battery |

Both boards are identical Nano ESP32s; the firmware assigns their roles.

---

## Repo layout

| Folder | What |
|--------|------|
| `remote_espnow/` | Remote firmware — joystick → ESP-NOW broadcast |
| `car_l293d/` | Car firmware — ESP-NOW → L293D shield, tank steering, demo |
| `car_motortest/` | One-motor-at-a-time diagnostic (no radio) for bring-up |
| `balance_mpu/` | **Experimental self-balancing mode** — MPU-6050 + PID (see below) |

---

## Toolchain

Built with [`arduino-cli`](https://arduino.github.io/arduino-cli/).

```bash
# core (once)
arduino-cli core install esp32:esp32          # built with 3.3.10
arduino-cli lib install "Adafruit MPU6050"    # only for balance_mpu

# compile
arduino-cli compile --fqbn esp32:esp32:nano_nora car_l293d
arduino-cli compile --fqbn esp32:esp32:nano_nora remote_espnow

# upload (Nano ESP32 uploads over DFU)
arduino-cli upload -p <PORT> --fqbn esp32:esp32:nano_nora car_l293d
```

> ⚠️ **DFU: one Nano at a time.** The Nano ESP32 uploads via `dfu-util`, which
> aborts with *"more than one DFU capable USB device"* if both boards are
> plugged in. Connect only the board you're flashing. Both can be connected for
> running / serial monitoring — just not for uploading.

Serial monitor is 115200 baud.

---

## Wiring

### Remote — KY-023 joystick → Nano ESP32
| Joystick | Nano | Note |
|----------|------|------|
| GND | GND | |
| +5V | **3V3** | ⚠️ 3.3V, **not** 5V (ESP ADC max) |
| VRx | A0 | |
| VRy | A1 | |
| SW  | D3 | button (INPUT_PULLUP) |

### Car — Nano ESP32 → L293D shield
All numbered holes are on the shield's **digital header** (`0…13`).

**Shift register (direction):**
| Nano | Shield hole | Purpose |
|------|-------------|---------|
| D2 | `12` | 74HC595 latch |
| D3 | `4`  | 74HC595 clock |
| D4 | `8`  | 74HC595 data |

**Motor PWM / enable** (Adafruit v1 mapping: M3 = hole 6, M4 = hole 5):
| Nano | Shield hole | Motor |
|------|-------------|-------|
| D5 | `11` | M1 |
| D6 | `3`  | M2 |
| D7 | `5`  | M4 |
| D8 | `6`  | M3 |

**Enable + power:**
| Connection | Purpose |
|------------|---------|
| shield `7` → GND | 74HC595 output-enable (active low) — required |
| Nano `5V` → shield `5V` | chip logic power (see [Power](#power)) |
| Nano GND → shield GND | common ground |
| Battery + → EXT_PWR `+M`, − → `GND`; PWR jumper in | motor power |

### Motor corner map
```
        FRONT
   M2 ┌─────┐ M1        LEFT side  = M2 (front-left) + M3 (back-left)
      │     │           RIGHT side = M1 (front-right) + M4 (back-right)
   M3 └─────┘ M4
        BACK
```
Tank mixing: `left = throttle + steer`, `right = throttle - steer`.

---

## Power

- **Bench / tethered:** the car Nano is USB-powered; its `5V` pin (USB VBUS)
  feeds the shield's logic. Battery on EXT_PWR drives the motors.
- **Cordless:** the Nano's on-board regulator only makes 3.3V — its `5V` pin is
  USB-only, so running on `VIN` alone leaves the shield logic unpowered and the
  motors dead. Add a **5V buck converter** off the battery:

```
Battery 11V ─┬─ shield EXT_PWR        (motor power)
             ├─ Nano VIN               (6–21V OK on Nano ESP32)
             └─ 5V buck ─ 5V ─ shield "5V" pin   (logic power)
GND: battery − ─ shield GND ─ Nano GND ─ buck GND   (all common)
```
Power the remote Nano from any USB power bank.

---

## Controls

- **Joystick:** forward/back = drive straight; left/right = turn (differential).
- **Triple-click the joystick button** (3 presses within ~1.2 s) → **demo show**:
  spin left, spin right, forward, back, a **figure-8**, then a wiggle finale,
  then control returns to you.
- **Failsafe:** if the car hears no packet for 400 ms, it stops.

### Sounds (the motors are the speaker)
The car plays tones **through the motor coils** — driven at a note's frequency
and a low duty (`TONE_DUTY`), the coils hum the pitch without spinning. During a
tone the front wheels are driven forward and the back wheels reverse, so the
forces cancel and the car stays put. All sounds fire from `loop()` (never the
radio callback) via `playMelody()`:

| Event | Sound |
|-------|-------|
| Boot | Imperial March opening |
| Remote first connects | two-note chirp |
| Joystick button press | single beep |
| Triple-click (demo) | fanfare, then the demo show |

Change any tune by editing its note/duration arrays (`MARCH_N/D`, `CHIRP_N/D`,
`BEEP_N/D`, `FANFARE_N/D`) near the top of `car_l293d.ino`. Notes are Hz,
durations ms, `0` = rest.

### Tuning (`car_l293d/car_l293d.ino`)
| Constant | Meaning |
|----------|---------|
| `MAX_DUTY` (180) | speed cap (0–255). Raise for more speed; watch motor heat on 11.1V |
| `MIN_DUTY` (75) | floor: any motion lifts to ≥ this so all motors break friction together |
| `TRIM_M1..4` (100) | per-motor speed trim (%). Bump a lagging wheel, e.g. `TRIM_M4 = 112` |
| `DEADZONE` (300) | joystick center dead-band (raw ADC counts) |
| `FAILSAFE_MS` (400) | stop if no packet for this long |

---

## Notes

- **L293D + 3.3V logic.** The shield's `74HC595` runs at 5V; a 3.3V ESP32 is
  marginal for its HIGH threshold, so the shift-out is deliberately **slowed**
  (`delayMicroseconds` around the clock edges). Fast bit-banging latched complex
  direction patterns unreliably.
- **PWM pin map.** Adafruit v1 uses **M3 = pin 6, M4 = pin 5** — mixing these up
  leaves two motors dead while the other two work.
- **Steering geometry.** Motors group by physical **side** (left/right), not by
  the M1–M4 numbering. See the corner map above.
- **Bring-up tip.** `car_motortest/` drives one motor at a time (no radio) — the
  fastest way to verify each wheel's pin mapping and spin direction.

---

## Self-balancing mode (experimental — `balance_mpu/`)

A proof-of-concept that stands the car **upright on its rear axle** (M3+M4) and
balances like an inverted pendulum, using an added **MPU-6050 (GY-521)** IMU and
a PID loop. **It works** — it holds the tilt angle to within ~±3° — but with
caveats (see below). This is a separate firmware from the RC car.

**Extra hardware:** MPU-6050 (GY-521) → `VCC→3V3, GND→GND, SCL→A5, SDA→A4`.
Mount it rigidly, low and near the rear axle.

**How it works:**
- Reads accel+gyro via the **Adafruit MPU6050** library, fuses them with a
  complementary filter into a tilt `angle`, and runs PID on `(angle - SETPOINT)`.
- **Feedforward kick** (`MIN_DRIVE`) lifts every correction past the TT-motor
  deadband, since small PID outputs otherwise can't move the motors.
- **Auto-arm / recover:** starts idle; arms when stood within `ARM_WINDOW` of the
  balance angle; disarms (motors off) past `FALL_LIMIT`; re-arms on stand-up —
  no power cycle needed after a fall.

**Bring-up:** flash with `#define CALIBRATE 1` (motors off), hold the car at its
teetering point, read the angle → that's your `SETPOINT`. Set `CALIBRATE 0`, do
the sign check at low `Kp` (flip `OUTPUT_SIGN` if wheels drive the wrong way),
then raise `Kp`/`Kd`. Tuned values here: `SETPOINT 67.6`, `Kp 20`, `Kd 0.8`.

**Known limits (honest):**
- **Translational drift** — with no wheel encoders it holds *angle*, not
  *position*, so it slowly rolls across the floor. Encoders (or a command-integral
  damper) are the real fix.
- **TT motors + L293D are marginal** for balancing (slow, backlash) — expect
  wobble-catching, not rock-steady standing. A TB6612/DRV8833 driver + faster
  motors would transform it.
- Powered off USB, hard motor stalls (during a fall) can brown out the board —
  power the logic from the battery+buck with a bulk cap for stability.
