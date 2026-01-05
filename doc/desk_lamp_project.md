# Desk Lamp Rebuild Project

## Project Overview

Rebuilding a failed table LED lamp using salvaged components (LED assembly and frame) with new electronics. The original lamp had unreliable charging circuitry and a suspiciously low-capacity battery. This rebuild aims to create a reliable, long-lasting lamp with better battery life and user-friendly low-battery warnings.

## Original Lamp Specifications

- USB-C charging port
- 18650 LiFePO4 battery (claimed 600mAh - suspicious)
- Three-wire LED connection: white LED, warm LED, and common ground
- Capacitive touch button (exposed metal with single wire)
- Simple on/cycle/off operation

**Failure mode:** Completely dead, even with mains power. Worked once after attempted charge, then never again.

## Design Goals

1. Reliable charging and power management
2. Improved battery capacity (2000-3000mAh)
3. Low-battery warning indicator
4. Simple operation (press to cycle modes, long-press for off)
5. Efficient power usage with deep sleep capability
6. Easy to build on perfboard with through-hole components

## Components List

### Core Electronics
- **ESP32-C3 SuperMini** development board (~£1-2)
  - Native USB-C for programming
  - 3.3V operation, runs 2.3V-3.6V
  - Deep sleep ~10µA
  - WiFi/BLE present but unused
- **TP4056 USB-C charger module** with protection
- **18650 Li-ion battery** (2000-3000mAh, standard 3.7V nominal)
- **TTP223 capacitive touch module**

### Switching and Control
- **2× N-channel MOSFETs** (TO-92 package for through-hole)
  - Suitable types: 2N7000, BS170
  - For switching LED strings to ground

### Passive Components
- **Voltage divider for battery monitoring:**
  - 100kΩ resistor (high side)
  - 27kΩ resistor (low side)
  - High impedance to minimise parasitic drain
- **LED current limiting resistors** (values TBD based on LED specifications)
- Pin headers for modular connections

## Wiring Diagram

```
                    USB-C
                      │
                 ┌────┴────┐
                 │  TP4056 │
                 │ Charger │
                 └─┬────┬──┘
                   │    │
        ┌──────────┘    └──────────┐
        │                          │
     B+ │                       B- │
   ┌────┴─────┐                   │
   │  18650   │                   │
   │  Li-ion  │                   │
   └────┬─────┘                   │
        │                         │
        │  100kΩ                  │
        ├─────┐                   │
        │     │                   │
        │    ─┴─ 27kΩ             │
        │     │                   │
        │     ├──────────────────→ A0 (battery monitor)
        │     │                   │
   OUT+ │    ─┴─                  │ OUT-
        │     │                   │
        └─────┼───────────────────┴──────┐
              │                          │
         VCC (bypassed regulator)       GND
              │                          │
         ┌────┴──────────────────────────┴────┐
         │      ESP32-C3 SuperMini            │
         │    (or Arduino Pro Mini 3.3V)      │
         │                                    │
         │  D2 (INT)  ←─────┐                │
         │  D9 (PWM)  ──┐   │                │
         │  D10 (PWM) ──┼───┼────────┐       │
         └──────────────┼───┼────────┼───────┘
                        │   │        │
                    ┌───┘   │        │
                    │   ┌───┴───┐    │
                    │   │TTP223 │    │
                    │   │ Touch │    │
                    │   └───┬───┘    │
                    │       │        │
                    │      VCC      GND
                    │       
                    │ Gate         Gate
                 ┌──┴──┐        ┌──┴──┐
                 │ Q1  │        │ Q2  │
                 │N-FET│        │N-FET│
                 └──┬──┘        └──┬──┘
                    │              │
                  Drain          Drain
                    │              │
              ┌─────┴──[R1]─┐     │
              │   White LED  │     │
              └──────────────┘     │
                    │              │
              ┌─────┴──[R2]────────┘
              │    Warm LED     │
              └─────────────────┘
                    │
                   GND (common cathode)
```

## Key Design Decisions

### Power Path
- **Battery connects directly to VCC pin** (bypassing onboard regulator if using Pro Mini)
- ESP32-C3 runs directly from battery voltage (2.3V-3.6V range)
- This allows full battery capacity usage down to ~3.0V
- Pro Mini option requires regulator bypass to avoid losing 40-50% of battery capacity

### Battery Monitoring
- **Simple voltage divider approach** (no fuel gauge IC needed)
- High-impedance divider (147kΩ total) minimises parasitic drain
- Uses ATmega328P/ESP32 internal 1.1V ADC reference
- Voltage scaling: 4.2V battery → 0.77V at ADC pin

### Battery Voltage Thresholds
- **4.2V** - Fully charged
- **3.7V** - ~50% (nominal voltage)
- **3.5V** - Low battery warning threshold
- **3.2V** - Critical, consider shutdown
- **3.0V** - Hard cutoff to protect battery

### ADC Configuration
For ESP32-C3 or ATmega328P with bypassed regulator:
```cpp
analogReference(INTERNAL); // Use 1.1V internal reference
```

This provides stable reference independent of varying battery voltage.

### Touch Sensing
- **TTP223 module recommended** over Arduino capacitive library
- Enables deep sleep mode (~10µA vs ~5-10mA)
- Simple 3-wire connection (VCC, GND, OUT)
- Active-high output suitable for interrupt wake-up

## State Machine Design

```
┌─────────────────────────────────────────────────┐
│  STARTUP                                        │
│  └→ Check battery voltage                      │
│     └→ If low: set warning flag                │
└────────────┬────────────────────────────────────┘
             │
             ▼
        ┌────────┐
        │  OFF   │ ◄──────────────┐
        │        │                │
        │ LEDs:  │                │
        │  Both  │                │
        │  off   │                │
        └────┬───┘                │
             │                    │
      Touch detected             │
             │               Long press
             ▼               (>2s)
        ┌────────┐               │
        │ WHITE  │───────────────┘
        │  FULL  │
        │        │
        │ LEDs:  │
        │  White │
        │  100%  │
        └────┬───┘
             │
      Touch detected
             │
             ▼
        ┌────────┐
        │  WARM  │
        │  FULL  │
        │        │
        │ LEDs:  │
        │  Warm  │
        │  100%  │
        └────┬───┘
             │
      Touch detected
             │
             ▼
        ┌────────┐
        │  BOTH  │
        │  FULL  │
        │        │
        │ LEDs:  │
        │  Both  │
        │  100%  │
        └────┬───┘
             │
      Touch detected
             │
             └──────► (cycle back to OFF)
```

### State Behaviours

**OFF State:**
- Both MOSFETs off
- MCU in deep sleep mode
- Wake on touch interrupt
- Minimal power draw (~10µA)

**ON States (WHITE / WARM / BOTH):**
- Appropriate MOSFET(s) enabled via PWM
- Periodic battery voltage checks
- Respond to touch for state changes
- Monitor for long-press (return to OFF)

**Low Battery Warning:**
When entering any ON state with battery voltage < 3.5V:
- Pulse all active LEDs 3× quickly before settling to steady state
- Alternative: Brief simultaneous flash of both LED strings

### Optional Future Features
- **Dimming mode:** Hold touch for >500ms to cycle through brightness levels (100% → 75% → 50% → 25%)
- **Memory:** Remember last used mode/brightness
- **Auto-off timer:** Sleep after X minutes of inactivity

## Software Architecture

### Core Functions Needed

1. **Touch detection** - Interrupt-driven on D2/INT pin
2. **PWM LED control** - Smooth brightness control on D9, D10
3. **Battery monitoring** - Periodic ADC reads on A0
4. **State management** - Handle state transitions and timing
5. **Sleep mode** - Deep sleep in OFF state, wake on interrupt
6. **Debouncing** - Clean touch event handling

### Pseudo-code Structure

```cpp
// Pin definitions
#define TOUCH_PIN 2        // Interrupt pin
#define WHITE_LED 9        // PWM pin
#define WARM_LED 10        // PWM pin
#define BATTERY_PIN A0     // ADC pin

// States
enum State {
  OFF,
  WHITE_FULL,
  WARM_FULL,
  BOTH_FULL
};

State currentState = OFF;
bool lowBatteryFlag = false;

void setup() {
  analogReference(INTERNAL);  // 1.1V reference
  pinMode(WHITE_LED, OUTPUT);
  pinMode(WARM_LED, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), touchISR, RISING);
  
  checkBatteryLevel();
}

void loop() {
  if (currentState == OFF) {
    enterDeepSleep();
  } else {
    updateLEDs();
    
    if (millis() % 10000 == 0) {  // Check every 10s
      checkBatteryLevel();
    }
    
    handleLongPress();  // Check for return to OFF
  }
}

void touchISR() {
  // Handle touch event
  // Advance state machine
  // Show low battery warning if flag set
}

void checkBatteryLevel() {
  int adcValue = analogRead(BATTERY_PIN);
  float voltage = adcValue * (1.1 / 1023.0) * (127.0 / 27.0);
  
  if (voltage < 3.5) {
    lowBatteryFlag = true;
  } else {
    lowBatteryFlag = false;
  }
}
```

## Build Notes

### Physical Construction
- Mount on perfboard inside lamp base
- 3D print insert/holder for clean mounting
- Use female pin headers for modularity
- Consider strain relief for LED wires
- Access to USB-C port for charging

### Component Selection
- **MOSFETs:** TO-92 package for easy through-hole soldering
- **Resistors:** Standard 1/4W through-hole resistors
- **Battery holder:** Secure mounting, consider spring contacts vs solder tabs

### Testing Checklist
- [ ] Verify voltage divider scaling (4.2V → ~0.77V at ADC)
- [ ] Test MOSFET switching at low battery voltages
- [ ] Measure standby current in OFF state
- [ ] Verify charging indication on TP4056
- [ ] Test all state transitions
- [ ] Verify low battery warning triggers correctly
- [ ] Measure runtime at different brightness levels

## Power Budget Estimates

### OFF State (Sleep)
- ESP32-C3 deep sleep: ~10µA
- TTP223 standby: ~1.5µA
- Voltage divider: ~15µA (assuming 3.7V / 147kΩ)
- **Total: ~26µA**
- **Standby time with 2500mAh battery: ~10 years** (theoretical)

### ON State (Active)
- ESP32-C3 active: ~80mA (worst case)
- White LED: ~20-50mA (estimate, depends on specs)
- Warm LED: ~20-50mA (estimate)
- **Total: 120-180mA** (both LEDs at full brightness)
- **Runtime with 2500mAh battery: ~14-20 hours** (continuous)

Actual runtime will be much longer with mixed usage and dimming.

## Safety Considerations

1. **Battery protection:** TP4056 module should include over-charge and over-discharge protection
2. **Current limiting:** Use appropriate resistors for LEDs to prevent thermal damage
3. **Voltage monitoring:** Implement hard cutoff at 3.0V to protect battery longevity
4. **Thermal management:** Ensure adequate ventilation in lamp base
5. **Short circuit protection:** Consider adding a fuse in battery positive line

## Future Improvements

### Possible Enhancements
- **OTA updates:** Use ESP32-C3 WiFi capability for wireless programming
- **Scheduling:** Auto-on/off at specific times
- **Ambient light sensor:** Auto-brightness adjustment
- **Phone app control:** BLE or WiFi interface
- **USB-C power delivery:** Use lamp as phone charger (pass-through)
- **Multi-colour LEDs:** RGB strips for colour temperature adjustment

### Why ESP32-C3 Was Chosen
Even though wireless features aren't initially needed:
- Same or lower cost than Pro Mini
- Native USB-C (no FTDI adapter)
- Superior power efficiency in sleep mode
- Better ADC resolution (12-bit vs 10-bit)
- Future-proof for feature additions
- Modern toolchain and library support
- WiFi/BLE stay dormant if unused (zero complexity added)

## Bill of Materials (AliExpress Order)

### Essential
- [ ] ESP32-C3 SuperMini (×2-3 for spares)
- [ ] TP4056 USB-C charger modules (×3-5)
- [ ] TTP223 capacitive touch modules (×5-10 pack)
- [ ] 2N7000 or BS170 N-FETs (×10 pack)
- [ ] 18650 Li-ion battery (2500-3000mAh, protected cells)
- [ ] Resistor assortment (1/4W through-hole)

### Nice to Have
- [ ] 18650 battery holders (panel mount)
- [ ] Female pin headers strips
- [ ] Perfboard (various sizes)
- [ ] USB-C breakout boards
- [ ] Small slide/toggle switches
- [ ] Heatshrink tubing assortment

## Document Revision History

- **v1.0** - Initial project documentation
- Created: January 2026