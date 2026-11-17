# AMDRadeonRembrandtHook

[简体中文](README_zh-CN.md)

## What is this?

An experimental [Lilu](https://github.com/acidanthera/Lilu) plugin for AMD Rembrandt iGPUs on Hackintosh.

## What can it do?

It patches Apple's `AMDRadeonX6000` driver stack for Rembrandt, including PCI matching, VBIOS from ACPI VFCT, firmware, SMU and framebuffer hooks. The goal is native display output and Metal acceleration.

## Can I use it?

This project targets Rembrandt integrated graphics with PCI IDs `1002:1681` or `1002:164d` in these AMD Ryzen processors:

- Ryzen 7: `6800H`, `6800HS`, `6800U`, `7735HS`, `7735U`
- Ryzen 9: `6900HS`, `6900HX`, `6980HS`, `6980HX`
- Ryzen 5: `6600H`, `6600HS`, `6600U`, `7535HS`, `7535U`
- Ryzen 3: `7335U`

These CPU models are recognized for GPU name display; the list is not a claim that each machine has been tested. Other Rembrandt CPUs with a matching PCI ID use the generic name `AMD Radeon Graphics (Rembrandt)`. Metal acceleration has been observed on a Ryzen 7 6800HS system; the project remains experimental, and other CPUs and macOS versions need verification. The plugin declares macOS Big Sur through Tahoe, which is not a compatibility guarantee.

## How do I use it?

1. In BIOS or your system's graphics settings, select hybrid graphics mode first. If it is unavailable or does not work, try iGPU-only mode.
2. Add `Lilu.kext` and `AMDRadeonRembrandtHook.kext` to `EFI/OC/Kexts`, and enable them in OpenCore's `Kernel -> Add`, with Lilu first.
3. Boot with verbose logging (`-v`) and check the serial or kernel log for `AMDRadeonRembrandtHook`.
4. The first boot automatically consumes a one-time NVRAM test flag. To test again, delete `7C436110-AB2A-4BBB-A880-FE41995C9F82:AMDRadeonRembrandtHookTest` from NVRAM before rebooting. Once you have verified that the kext works, enable it persistently from macOS:

   ```sh
   sudo nvram '7C436110-AB2A-4BBB-A880-FE41995C9F82:AMDRadeonRembrandtHookEnabled=%01'
   ```

Keep a working EFI available for recovery. The machine must expose its own VBIOS through the ACPI `VFCT` table.

The kext fills missing PSP and VRAM tables when it recognizes the ATOM layout. It reads UMA size from the GPU and memory width from VBIOS. If either cannot be read, add `rmb-vram-size-mb` and `rmb-memory-width` as 32-bit little-endian `Data` properties on the GPU in OpenCore DeviceProperties; for example, 512 MiB and 128 bits are `00020000` and `80000000`. The VRAM table is skipped if valid values are unavailable.

## How can I build it myself?

```sh
./prepare.sh
./build-macos.sh   # macOS
./build-linux.sh   # Linux
```

The kext is written to `build/AMDRadeonRembrandtHook.kext`. Linux requires Clang 21 and an Apple cctools `ld64` linker at `upstream/cctools/bin/x86_64-apple-darwin19-ld`, or a path supplied via `CCTOOLS_LD`. `prepare.sh` fetches cctools-port source but does not build the linker.

## Acknowledgments

- [Acidanthera](https://github.com/acidanthera) for Lilu, OpenCore and MacKernelSDK.
- [RembrandtGfx](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh) and [Radeonized](https://github.com/FranciumOS-org/Radeonized) for Rembrandt research.
- [660m-Metal](https://github.com/rthh21/660m-Metal) for ATOM VBIOS compatibility table research.
- [NootedRed](https://github.com/ChefKissInc/NootedRed) for AMD iGPU research.
- [cctools-port](https://github.com/tpoechtrager/cctools-port) and the Linux AMDGPU contributors.
