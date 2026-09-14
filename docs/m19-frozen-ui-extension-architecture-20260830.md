# M19 冻结 UI 的扩展架构审计（2026-08-30）

## 结论

当前已验收 UI/触摸镜像不是可动态扩展的平台。它可以安全地继续作为
显示与触摸回归基线，但不能在保持该镜像不变的条件下加载新的工作流、
相机控制或模型运行程序。

这是架构边界，不是“再换一个 ROMFS 命令”可以解决的问题。

## 可重复的离线反馈环

稳定配置快照：

```text
/home/max/openvela-p4-stable-recovery-isolated-20260829/nuttx/
  .config.pre-ui-only-touch-build-20260821-1703
```

稳定 ELF：

```text
/home/max/openvela-p4-stable-recovery-isolated-20260829/nuttx/nuttx
```

已执行的审计命令等价于：

```sh
grep -E 'CONFIG_(MODULE|FS_ROMFS|PIC|NXFLAT|ELF)' <stable-config>
riscv32-esp-elf-nm -C --defined-only <stable-elf>
```

判定结果：

| 条件 | 结果 | 含义 |
| --- | --- | --- |
| `CONFIG_FS_ROMFS` | 未启用 | 没有可挂载的 ROMFS 扩展载体 |
| `CONFIG_ELF` | 未启用 | 没有运行时 ELF 装载器 |
| `CONFIG_NXFLAT` | 未启用 | 没有 NXFLAT 装载器 |
| `CONFIG_MODULE` | 未启用 | 没有可加载模块框架 |
| `CONFIG_PIC` | 未启用 | 不能安全装载位置无关扩展代码 |
| `CONFIG_BINFMT_ELF_RELOCATABLE` | 已启用 | 仅表示构建阶段生成可重定位 ELF，**不**表示目标机具备加载 ELF 的能力 |

稳定 ELF 内仍存在 `camera_diag_main`、SC2336 CSI/ISP 和 DSI/GT911 的
已链接符号；但没有 TFLite Micro、模型容器读取、工作流状态机、ELF/NXFLAT
加载或模块装载符号。因此它不能从现有代码中“打开”模型功能。

## 已排除的路径

1. 重新链接丰富 UI/工作流镜像：多个候选已实屏黑屏或蓝屏，不能作为基线升级方式。
2. 在稳定镜像上增加一小段后链接代码：此前仅增加四字节锚点仍改变最终封装长度，
   未通过启动等价门禁，未上板。
3. 把 `BINFMT_ELF_RELOCATABLE` 当作动态加载支持：这是构建格式误读，已排除。

## 等长二进制补丁容量结论

对精确稳定 ELF 的 `.flash.text` 映射和反汇编进行了检查。最大的连续填充是
`0x66`（102 字节，`0x4001199a..0x400119ff`），其次为 `0x10`（16 字节）；其余
都是 1--4 字节对齐填充。反汇编显示前者位于 `mm_shrinkchunk()` 结尾与
`up_saveusercontext()` 起点之间，后者位于一个函数返回与 `group_bind()` 起点之间。

这些是链接对齐空洞，不是可复用的未调用程序。即便把它们当作可写空间，也不足以
容纳工作流状态机、模型容器读取、TFLite Micro 或安全的调用/错误处理代码；同时还需
修改现有触摸控制流才能到达该代码。故“在稳定镜像内找等长闲置代码区完成完整项目”的
路线已被容量与控制流审计否决，不能上板。

## 后续唯一值得继续的低风险研究路径

完整项目需要新的、可恢复的系统镜像：启动镜像本身必须在首次构建时包含所需的
DSI/GT911 驱动、工作流、模型运行时和一个可验证的启动路径。每个候选仍只写
`0x2000`、保留模型容器、先通过 UART 启动指纹，再做实屏与触摸验收，并在失败时
自动恢复冻结镜像。不能承诺在当前冻结镜像中完成八按钮和模型功能。

本审计没有读取或写入板卡 Flash，也没有访问模型容器 `0xE0000`。
