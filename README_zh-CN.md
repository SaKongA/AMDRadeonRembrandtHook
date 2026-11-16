# AMDRadeonRembrandtHook

[English](README.md)

## 这是什么？

一个让 AMD Rembrandt 核显在黑苹果上工作的实验性 [Lilu](https://github.com/acidanthera/Lilu) 插件。

## 它能做什么？

给 Apple 的 `AMDRadeonX6000` 驱动打补丁，处理 Rembrandt 的 PCI 匹配、VBIOS、固件、SMU 和 framebuffer。目标是点亮屏幕并实现 Metal 加速。

## 我能使用吗？

面向 PCI ID 为 `1002:1681` 或 `1002:164d` 的 **AMD Radeon 680M 和 660M** 核显。

目前还在**实验阶段**，图形加速尚未确认可用，可能会遇到内核崩溃或显示异常。插件允许在 macOS Big Sur 至 Tahoe 加载，但这些系统版本还没有全部验证。

## 如何使用？

1. 在 BIOS 或系统显卡设置中先选混合模式；如果不行，再试仅集显模式。
2. 把 `Lilu.kext` 和 `AMDRadeonRembrandtHook.kext` 放进 `EFI/OC/Kexts`，在 OpenCore 的 `Kernel -> Add` 中启用，Lilu 放在前面。
3. 加上启动参数 `-v`，启动后在串口或内核日志中搜索 `AMDRadeonRembrandtHook`。
4. 第一次启动会自动写入一次性 NVRAM 测试标记。想再测一次，就在重启前删除 NVRAM 中的 `7C436110-AB2A-4BBB-A880-FE41995C9F82:AMDRadeonRembrandtHookTest`。确认 kext 没问题后，可以在 macOS 中设置为一直启用：

   ```sh
   sudo nvram '7C436110-AB2A-4BBB-A880-FE41995C9F82:AMDRadeonRembrandtHookEnabled=%01'
   ```

记得留一份能正常启动的 EFI 备用。机器还需要通过 ACPI `VFCT` 表提供本机 VBIOS。

## 如何自行构建？

```sh
./prepare.sh
./build-macos.sh   # macOS
./build-linux.sh   # Linux
```

构建好的 kext 在 `build/AMDRadeonRembrandtHook.kext`。Linux 下需要 Clang 21 和 Apple cctools `ld64` 链接器：默认从 `upstream/cctools/bin/x86_64-apple-darwin19-ld` 查找，也可以用 `CCTOOLS_LD` 指定路径。`prepare.sh` 只拉取 cctools-port 源码，不会构建链接器。

## 致谢

- [Acidanthera](https://github.com/acidanthera)：Lilu、OpenCore 和 MacKernelSDK。
- [RembrandtGfx](https://github.com/caoshiyuan0211-blip/winmax2-2022-6800u-hackintosh) 和 [Radeonized](https://github.com/FranciumOS-org/Radeonized)：Rembrandt 相关研究。
- [NootedRed](https://github.com/ChefKissInc/NootedRed)：AMD 核显相关研究。
- [cctools-port](https://github.com/tpoechtrager/cctools-port) 及 Linux AMDGPU 贡献者。
