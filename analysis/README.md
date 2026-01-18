# LED Desk Lamp Power Analysis

Tools for measuring power consumption and predicting battery runtime.

## Overview

This folder contains templates and analysis tools for characterizing the lamp's power consumption across the battery discharge range (4.2V → 3.0V).

## Methodology

1. Connect adjustable power supply to the **5V input** of the ESP32-C3 SuperMini
2. Set the lamp to the desired mode (e.g., WHITE at MAX brightness)
3. Step through voltages from 4.2V down to 3.0V (in 0.1V or 0.2V steps)
4. At each voltage, measure the current draw
5. Record measurements in CSV file
6. Analyze using Python script or spreadsheet

## File Organization

```
analysis/
├── README.md                          # This file
├── templates/                         # CSV templates for different test scenarios
│   ├── white_max_brightness_with_charger.csv
│   ├── white_max_brightness_no_charger.csv
│   ├── warm_max_brightness_with_charger.csv
│   └── off_deep_sleep.csv
├── data/                              # Put your actual measurements here
│   └── (your CSV files go here)
├── analyze_runtime.py                 # Python analysis script
└── runtime_calculator_template.csv   # Spreadsheet template
```

## Test Scenarios

### Recommended Tests

1. **WHITE at MAX brightness (with charger chip)**
   - Most common use case
   - Includes TP4056 protection MOSFET resistance

2. **WHITE at MAX brightness (no charger chip)**
   - Bypass TP4056 to measure MCU + LED only
   - Helps isolate charger chip overhead

3. **WARM at MAX brightness (with charger chip)**
   - Optional: if you want to compare white vs warm LEDs

4. **OFF (deep sleep)**
   - Should be ~26µA constant across voltage range
   - Validates standby power consumption

### Additional Tests (Optional)

- Mid brightness levels (e.g., 50%, 128/255)
- Mixed mode (both LEDs on)
- During transitions (crossfading)

## Using the CSV Templates

1. Copy a template from `templates/` to `data/`
2. Rename it to describe your test (e.g., `data/white_max_2026-01-17.csv`)
3. Fill in the current measurements
4. Analyze with Python script or spreadsheet

**CSV Format:**
```csv
voltage,current_mA
4.2,85.3
4.1,84.7
4.0,84.1
...
3.0,80.2
```

## Python Analysis Script

### Requirements

Python 3.6+ (no external dependencies needed)

### Usage

**Analyze a single file:**
```bash
python analyze_runtime.py data/white_max_with_charger.csv
```

**Analyze multiple files:**
```bash
python analyze_runtime.py data/*.csv
```

**Custom battery capacities:**
```bash
python analyze_runtime.py data/white_max.csv --battery 2000 2500 3000 3500
```

### Output

The script will display:
- Voltage range tested
- Min/max/average current draw
- Estimated runtime for different battery capacities
- Average power consumption

Example output:
```
======================================================================
Analyzing: white_max_with_charger.csv
======================================================================
  Voltage range: 3.0V - 4.2V
  Number of measurements: 13

  Current draw:
    Minimum:        80.2 mA
    Maximum:        85.3 mA
    Simple average: 82.5 mA
    Weighted avg:   82.3 mA

  Estimated runtime (using simple average):
    2000 mAh battery: 24.2 hours (1.0 days)
    2500 mAh battery: 30.3 hours (1.3 days)
    3000 mAh battery: 36.4 hours (1.5 days)

  Average power consumption: 305.3 mW (3.70V × 82.50mA)
```

## Spreadsheet Calculator

### Usage

1. Open `runtime_calculator_template.csv` in Excel, LibreOffice Calc, or Google Sheets
2. Paste your voltage measurements in column A (starting row 6)
3. Paste your current measurements in column B (starting row 6)
4. The formulas will automatically calculate:
   - Runtime for 2000/2500/3000 mAh batteries
   - Min/max/average statistics
   - Average power consumption

### Formulas Explained

If formulas don't auto-calculate, here are the key ones:

**Average current:**
```
=AVERAGE(B6:B18)
```

**Runtime (hours):**
```
=BatteryCapacity_mAh / AverageCurrent_mA
```

**Runtime (days):**
```
=RuntimeHours / 24
```

**Average power (mW):**
```
=AVERAGE(A6:A18) * AVERAGE(B6:B18)
```

## Tips for Accurate Measurements

### Power Supply Setup

- Use a quality adjustable power supply with current readout
- Connect to the **5V pin** on the ESP32-C3 SuperMini
- Connect ground to **GND**
- Set current limit to ~500mA (safety)

### Measurement Procedure

1. **Stabilize:** Wait 5-10 seconds after each voltage change
2. **Record:** Note the stabilized current reading
3. **Precision:** Measure to 0.1 mA if possible (most DMMs can do this)
4. **Test order:** Start at 4.2V, work down to 3.0V
5. **Firmware:** Ensure lamp is in the correct mode (use Serial debug output)

### With/Without Charger Test

**With charger:**
- Battery → TP4056 OUT+ → 5V pin
- Normal operating configuration

**Without charger:**
- Battery → directly to 5V pin (bypass TP4056)
- Measures just MCU + LED power
- Difference shows TP4056 MOSFET + IC overhead

### Deep Sleep Test

For OFF mode testing:
- Current will be very low (~26µA = 0.026mA)
- May need a µCurrent or specialized low-current meter
- Alternatively, use power supply's µA range if available
- Typical multimeters may not have sufficient resolution

## Expected Results

### WHITE at MAX brightness (preliminary estimates)

- Current: ~80-90 mA (across 3.0-4.2V range)
- Runtime with 2500 mAh: ~28-31 hours
- Power: ~300-350 mW

### OFF (deep sleep)

- Current: ~0.026 mA (26 µA)
- Standby time with 2500 mAh: ~10 years (theoretical)

### TP4056 Overhead

- Expected: 1-5 mA additional current
- MOSFET Rds(on): ~20-30 mΩ typical
- At 85 mA: voltage drop ~1.7-2.6 mV (negligible)

## Analysis Notes

### Simple vs Weighted Averaging

**Simple average:**
- Assumes equal time spent at each voltage
- Good approximation for Li-ion (relatively flat discharge curve)
- Easy to calculate manually

**Weighted average:**
- Accounts for actual discharge curve shape
- More accurate for highly non-linear current curves
- Python script provides both

### Battery Capacity Reality

Actual runtime will be affected by:
- Battery age and condition
- Temperature (cold reduces capacity)
- Discharge rate (high current reduces capacity)
- Protection circuit cutoff voltage
- Typical usable capacity: 80-95% of rated capacity

**Recommendation:** Use conservative estimates (e.g., 2400 mAh for a "2500 mAh" battery)

## Future Enhancements

- [ ] Add plotting capability (matplotlib visualization)
- [ ] Export results to formatted report
- [ ] Compare multiple test runs (before/after changes)
- [ ] Temperature compensation calculations
- [ ] Battery degradation modeling
