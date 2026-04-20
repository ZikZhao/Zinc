import subprocess
import re
import statistics
import sys

cmd = ["./main", "./model/cornell-box/scene.txt", "./model/cornell-box/sphere_brass.txt"]
# cmd = ["./build/CG-CW", "./model/cornell-box/scene.txt", "./model/cornell-box/sphere_brass.txt"]
timeout_seconds = 200

print(f"Executing: {' '.join(cmd)}")
print(f"Process will be terminated after {timeout_seconds} seconds...")

stdout_data = ""

try:
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_seconds)
    stdout_data = proc.stdout
except subprocess.TimeoutExpired as e:
    stdout_data = e.stdout

if isinstance(stdout_data, bytes):
    stdout_data = stdout_data.decode('utf-8', errors='replace')
elif stdout_data is None:
    stdout_data = ""

if not stdout_data:
    print("Error: No standard output captured from the process.")
    sys.exit(1)

lines = stdout_data.strip().split('\n')[-100:]

fps_pattern = re.compile(r"\[Renderer\] FPS:\s*([\d\.]+)\s*\|")
fps_values = []

for line in lines:
    match = fps_pattern.search(line)
    if match:
        fps_values.append(float(match.group(1)))

if not fps_values:
    print("Error: Could not find valid FPS data in the last 100 lines.\nOutput snippet:\n" + "\n".join(lines[:10]))
    sys.exit(1)

quantiles = statistics.quantiles(fps_values, n=4)

print("\n--- Benchmark Results ---")
print(f"Valid sample size: {len(fps_values)} frames")
print(f"Average (Mean):    {statistics.fmean(fps_values):.2f}")
print(f"Median:            {statistics.median(fps_values):.2f}")
print(f"25th Percentile:   {quantiles[0]:.2f}")
print(f"75th Percentile:   {quantiles[2]:.2f}")