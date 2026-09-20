# N32L406RB Flash and Debug

## Setup

Install the recommended Cortex-Debug and Tasks extensions, then run
`Developer: Reload Window`. Configure `n32.jlinkPath`, `n32.gccBin`,
`n32.device`, and `n32.swdSpeed` in workspace settings.
Connect the powered N32L406RB board over J-Link SWD, with ground and VTref.
Close J-Flash and other debug sessions before using the probe.

## Flash

Click `JLink-Flash` in the status bar, or select `Tasks: Run Task` -> `JLink-Flash`.
Build Debug or Release first, as in the reference STM32 project.
The task selects the newest ELF by modification time from `build/Debug` and
`build/Release`, generates a fresh BIN from that ELF, and downloads it to
`0x08000000`, verifies by reading back, resets the board, and runs the firmware.
The flash task does not build automatically. If a build fails, fix it and build
successfully before flashing; otherwise an older existing ELF could be selected.
Check the selected ELF path in the task output. The log is saved to
`jlink-flash.log` beside that ELF.
Success prints `Flash verified; target running.`

## Debug

Select `N32L406RB: J-Link Debug` in Run and Debug, then press F5.
This builds Debug, downloads the ELF, and stops at `main`.
The reference project's `jlinkgdbtarget` and `st-stm32-ide-debug-launch` commands
are STM32 extension-specific; this project uses Cortex-Debug for N32 instead.
Do not use CMake's Run/Launch command: it tries to execute the ARM ELF on Windows.
Use F9 for breakpoints, F10 to step over, F11 to step into, and Shift+F5 to stop.
Watch `g_thread_a_count`, `g_thread_b_count`, and `g_rtos_error` in the sample.
Do not flash while a debug session owns the probe.

Ctrl+Shift+B remains build-only. Both workflows use J-Link V9.76a and SWD at 1000 kHz.
The existing 128 KB Flash / 16 KB RAM application layout is unchanged;
the linker filename retains its original CB name. No chip unlock or Option Bytes
modifications are requested by these workflows.