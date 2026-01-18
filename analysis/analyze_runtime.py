#!/usr/bin/env python3
"""
LED Desk Lamp Runtime Analysis

Analyzes power consumption measurements across battery discharge range
to predict runtime with different battery capacities.

Usage:
    python analyze_runtime.py data/your_measurement.csv
    python analyze_runtime.py data/*.csv  # Analyze all CSV files
"""

import csv
import sys
from pathlib import Path
from typing import List, Tuple
import argparse


def load_measurement(csv_file: Path) -> List[Tuple[float, float]]:
    """Load voltage and current measurements from CSV file.

    Returns:
        List of (voltage, current_mA) tuples, sorted by voltage descending
    """
    data = []
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            voltage = float(row['voltage'])
            current = float(row['current_mA'])
            data.append((voltage, current))

    # Sort by voltage descending (4.2V -> 3.0V)
    data.sort(reverse=True, key=lambda x: x[0])
    return data


def calculate_average_current(data: List[Tuple[float, float]]) -> float:
    """Calculate average current draw across the discharge curve.

    For runtime estimation, we use trapezoidal integration weighted by
    the voltage range, assuming linear discharge.
    """
    if not data:
        return 0.0

    # Simple average (assumes equal time spent at each voltage)
    total_current = sum(current for _, current in data)
    return total_current / len(data)


def calculate_weighted_current(data: List[Tuple[float, float]]) -> float:
    """Calculate weighted average current using trapezoidal integration.

    This accounts for the fact that a Li-ion battery spends more time
    at certain voltages during discharge.
    """
    if len(data) < 2:
        return data[0][1] if data else 0.0

    total_energy = 0.0  # mWh
    total_capacity = 0.0  # mAh

    for i in range(len(data) - 1):
        v1, i1 = data[i]
        v2, i2 = data[i + 1]

        # Voltage difference (should be negative as we go from high to low)
        dV = abs(v1 - v2)

        # Average voltage and current in this segment
        v_avg = (v1 + v2) / 2
        i_avg = (i1 + i2) / 2

        # Power in this segment (mW)
        p_avg = v_avg * i_avg

        # For a linear discharge model, we assume equal time per voltage step
        # Energy consumed in this segment (mWh) - proportional to voltage range
        total_energy += p_avg * dV
        total_capacity += i_avg * dV

    # Return weighted average current
    if total_capacity > 0:
        return total_energy / (sum(v for v, _ in data) / len(data))
    return calculate_average_current(data)


def estimate_runtime(avg_current_mA: float, battery_capacity_mAh: float) -> float:
    """Estimate runtime in hours.

    Args:
        avg_current_mA: Average current draw in mA
        battery_capacity_mAh: Battery capacity in mAh

    Returns:
        Runtime in hours
    """
    if avg_current_mA <= 0:
        return float('inf')
    return battery_capacity_mAh / avg_current_mA


def analyze_file(csv_file: Path, battery_capacities: List[int] = [2000, 2500, 3000]):
    """Analyze a single measurement file and print results."""
    print(f"\n{'='*70}")
    print(f"Analyzing: {csv_file.name}")
    print(f"{'='*70}")

    try:
        data = load_measurement(csv_file)

        if not data:
            print("  ERROR: No valid data found in file")
            return

        print(f"  Voltage range: {min(v for v, _ in data):.1f}V - {max(v for v, _ in data):.1f}V")
        print(f"  Number of measurements: {len(data)}")

        # Calculate current statistics
        currents = [i for _, i in data]
        avg_current = calculate_average_current(data)
        weighted_current = calculate_weighted_current(data)
        min_current = min(currents)
        max_current = max(currents)

        print(f"\n  Current draw:")
        print(f"    Minimum:        {min_current:.2f} mA")
        print(f"    Maximum:        {max_current:.2f} mA")
        print(f"    Simple average: {avg_current:.2f} mA")
        print(f"    Weighted avg:   {weighted_current:.2f} mA")

        # Calculate runtime for different battery capacities
        print(f"\n  Estimated runtime (using simple average):")
        for capacity in battery_capacities:
            runtime = estimate_runtime(avg_current, capacity)
            print(f"    {capacity} mAh battery: {runtime:.1f} hours ({runtime/24:.1f} days)")

        # Power consumption
        voltages = [v for v, _ in data]
        avg_voltage = sum(voltages) / len(voltages)
        avg_power = avg_voltage * avg_current
        print(f"\n  Average power consumption: {avg_power:.1f} mW ({avg_voltage:.2f}V × {avg_current:.2f}mA)")

    except Exception as e:
        print(f"  ERROR: Failed to analyze file: {e}")


def main():
    parser = argparse.ArgumentParser(
        description='Analyze LED lamp power measurements and estimate battery runtime'
    )
    parser.add_argument(
        'csv_files',
        nargs='+',
        type=Path,
        help='One or more CSV files with voltage,current_mA measurements'
    )
    parser.add_argument(
        '--battery',
        nargs='+',
        type=int,
        default=[2000, 2500, 3000],
        help='Battery capacities to test (in mAh). Default: 2000 2500 3000'
    )

    args = parser.parse_args()

    print("LED Desk Lamp Runtime Analysis")
    print(f"Battery capacities: {', '.join(str(c) + ' mAh' for c in args.battery)}")

    # Process each file
    for csv_file in args.csv_files:
        if not csv_file.exists():
            print(f"\nWARNING: File not found: {csv_file}")
            continue
        analyze_file(csv_file, args.battery)

    print(f"\n{'='*70}")
    print("Analysis complete!")
    print(f"{'='*70}\n")


if __name__ == '__main__':
    main()
