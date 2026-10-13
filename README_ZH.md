<p align="center">
  <img src="assets/logo.png" alt="HyperHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>HyperHEN</b><br/>
  面向 PlayStation 5 的一体化 HEN 与工具箱
</p>

<p align="center">
  <b>简体中文</b>
  ·
  <a href="README.md">English</a>
</p>

<p align="center">
  <b><a href="#功能">功能</a></b>
  ·
  <b><a href="#运行">运行</a></b>
  ·
  <b><a href="#构建">构建</a></b>
  ·
  <b><a href="#配置">配置</a></b>
  ·
  <b><a href="#致谢">致谢</a></b>
</p>

---

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

<p align="center">
  HyperHEN 是一套模块化的 PS5 payload。一个入口 ELF 完成系统准备、注入<br/>
  ShellUI 工具箱，再拉起监控条、金手指、用户 payload 和后台服务。
</p>

<br>

# 功能

HyperHEN 面向已越狱的 PS5，提供一套能日常使用、也方便维护的自制环境。

- **ShellUI 工具箱** — 注入 PS5 ShellUI 的设置页
- **系统准备** — 提权、重新挂载文件系统、阻断系统更新分区
- **HyperCore** — 从源码构建并内嵌 kstuff + A53/PPR + BackPork；默认加载
- **插件运行时** — 发现、校验、启动、停止和删除带 descriptor 的插件 ELF
- **PS5 FTP 服务器** — 由可选的外部 `onionHEN-ftpsrv-plugin` 提供
- **HyperHEN Automount** — 从 ShadowMountPlus 源码构建并内嵌，统一负责 USB、Title、镜像与 fakelib 挂载生命周期
- **远程游玩配对** — 在网络菜单中启用 PS5 原生远程游玩服务、生成配对 PIN 并注册客户端
- **用户 Payload 管理** — 启动和停止用户添加的普通 `.elf` payload，可选自动启动
- **游戏监控条** — 游戏中显示 FPS、CPU、GPU、内存、温度和网络信息
- **金手指** — 本地 JSON、SHN、MC4、ShnExt 文件，运行中即可开关
- **主机工具** — 账号激活、外接硬盘、Title ID、风扇、快捷键和游戏选项
- **HyperDPI** — 从源码构建并内嵌的浏览器网络包安装器，设置页可直接控制
- **应用越狱** — 白名单自制软件可通过守护进程沙盒 FIFO 申请提权
- **可恢复运行时** — 关键守护进程和工具守护进程分开；主进程可以拉起工具进程
- **统一配置** — 工具箱和守护进程共用一份带版本号的 `config.ini`

HyperHEN 不内置内核漏洞。第一次引导仍需要外部 **9021** 上的 `elfldr`。
之后会启动自己的私有 **9020** 加载器，用来加载后续 ELF 和用户 payload。

<br>

# 运行

### 运行要求

1. 一台已越狱的 PS5
2. 一个监听 **9021**、用于第一次引导的外部 [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr)

### 加载 HyperHEN

1. 运行内核漏洞利用，并启动外部 `elfldr` 服务。
2. 用漏洞加载端提供的加载器发送 `HyperHEN.elf`。
3. 按顺序等待工具守护进程、HyperCore 和主守护进程启动。
4. 打开 PS5 设置，进入 HyperHEN 工具箱。

启动是按固定顺序进行的。第一次引导之后，HyperHEN 使用自己的
`hyperhen_elfldr.elf`（端口 **9020**），**9021** 只作为兼容回退。

```text
HyperHEN.elf → bootstrapper → 固件路由 → HyperHEN Automount → daemon.elf / Toolbox（可用时）
```

HyperCore 就绪后，daemon 会发现并启动外部插件。

### FTP 服务器

FTP 由独立的
[`onionHEN-ftpsrv-plugin`](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin)
提供。将其安装为 `/data/HyperHEN/plugins/FTPS00001.elf` 后，HyperHEN 会自动发现
并启动；插件动态设置页提供启用、端口和重启控制。插件保留上游 `ftpsrv` 的
`KILL`、`SELF`、`SCHK`、`MTRW`、`AUTHID` 等命令（视固件支持而定）。

### 挂载集成

HyperHEN 将 ShadowMountPlus 作为唯一的内嵌 automount 引擎。启动 HyperCore 前，
bootstrapper 会声明挂载所有权，使 HyperCore 原生扫描器和 BackPork 监控保持暂停，
避免两个引擎同时修改挂载状态。不要再加载外部 ShadowMountPlus 或第二个 BackPork。
当固件没有经过验证的 ShellCore 偏移时，共享生命周期监控器会通过 SysCore 子进程
事件在 exec 前准备 backport 与 fakelib 层。因此 1.05、1.10、1.11、1.13 和 4.02
不需要猜测偏移；具有精确配置的固件仍使用更早的 ShellCore bridge 路径。

### HyperDPI（网络包安装器）

HyperDPI 从固定版本的 GPL 源码构建，嵌入主 payload，并在 HyperHEN daemon
就绪后直接从内存启动；不再需要 `DPIV00001.elf` 或
`/data/HyperHEN/plugins` 下的外部文件。动态设置页提供启用、API 端口、WebUI
端口和重启控制，浏览器保留分块上传、队列管理、暂存文件复用和 SSE 实时进度。
默认 WebUI 地址是 `http://<console>:12800`。

首次启动时网络监听默认关闭。启用后 HyperDPI 会监听主机网络接口且不进行客户端
认证，因此只应在可信局域网使用，用完后请关闭。

### 远程游玩

远程游玩配对位于 **工具箱 → 网络 → 远程游玩**。
当前账号必须已经激活；未激活时，HyperHEN 会阻止进入远程游玩页面，并提示前往
**工具箱 → 账号 → 账号激活**。账号激活后，HyperHEN 会启用远程游玩注册表设置并显示
配对 PIN。保持页面打开，在官方远程游玩客户端中输入该 PIN；HyperHEN 会调用 PS5
原生远程游玩服务确认已注册的设备。

此功能只负责配对和设备注册。视频串流与客户端传输仍由 Sony 的原生远程游玩服务处理。

### Payload

把独立 payload 放到：

```text
/data/HyperHEN/payloads/
```

只支持普通 `.elf`。可在工具箱里打开自动启动；HyperHEN 会在 ELF 旁边写一个
同名的 `.auto_start` 文件记住这个选择。

所有 `.elf` 文件名都使用相同的 Payload 页面、加载器和自动启动流程。已有有效 PID
记录的用户 Payload 会保持运行，后续启动和自动启动请求直接跳过。
HyperHEN 插件属于独立类别，应放在 `/data/HyperHEN/plugins/`。

### 金手指

把金手指文件放到同一目录。文件名为 `TITLEID_VERSION`，进程名和 8 位十六进制 `SOURCE_ID` 都可省略：

```text
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].json
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].shn
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].mc4
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].ShnExt
```

`SOURCE_ID` 只用于标识物理来源。没有 `PROCESS` 的文件是通用来源；带进程名的文件只匹配对应进程。相同游戏、版本和进程的多个 JSON、SHN、MC4、ShnExt 来源会同时加载。

金手指从磁盘加载。文件有改动时会重新载入，不必重启整套进程。

`DOWNLOAD_CHEATS` 会通过 HTTPS 下载金手指仓库 ZIP，解压 `cheats/` 子树，再把 `json/`、`shn/`、`mc4/` 里的文件按原名拷进上述目录；同步完成后会清理临时 ZIP。镜像由 `[cheats] mirror` 控制：`auto` 时简体中文走 cnb.cool，其它地区走 GitHub。

<br>

# 构建

### 依赖

| 依赖 | 用途 |
| --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Prospero 编译器、目标头文件、运行库和 CMake 封装 |
| CMake 3.20+ 与 Ninja | 配置并构建整套 payload |
| Clang / LLVM | 编译 `x86_64-sie-ps5` 目标 |
| `lzma` 或 `xz` | 压缩 bootstrapper |
| GNU Make、GCC、NASM/Yasm | 构建 vendored HyperCore 源码 |

### 完整构建

```shell
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

脚本会验证 vendored 依赖、从源码构建 HyperCore，并按所需顺序构建整条嵌入链。

使用 Docker 进行可复现构建：

```shell
docker compose build hyperhen-build
docker compose run --rm hyperhen-build
```

### 常用选项

| 选项 | 说明 |
| --- | --- |
| `--build-type Debug\|Release` | 选择 CMake 构建类型 |
| `--build-dir <path>` | 覆盖默认的 `build/` 目录 |
| `--skip-unpacker` | 构建完 bootstrapper 后停止 |

运行 `./scripts/build.sh --help` 查看完整选项列表。

### 构建产物

| 路径 | 说明 |
| --- | --- |
| `build/bin/HyperHEN.elf` | 发送到主机的最终 payload |
| `build/bin/bootstrapper.elf` | 未压缩的 bootstrapper |
| `build/bin/bootstrapper.elf.lzma` | 嵌入最终 payload 的 bootstrapper |
| `build/bin/daemon.elf` | 关键守护进程 |
| `build/bin/util.elf` | 工具守护进程 |
| `build/bin/shellui.elf` | Toolbox 注入 payload |
| `build/vendor/hypercore/kstuff.elf` | 从源码构建并内嵌的 HyperCore 中间产物 |
| `build/lib/*.a` | 本仓库静态库 |

最终构建产物都在 `build/`；HyperCore 上游 Makefile 会在
`third_party/kstuff-a53/` 下生成中间文件。不会使用上游预编译 ELF。

### 测试

主机侧测试覆盖配置、IPC、payload 辅助逻辑、金手指解析、重定位、工具箱路由和共享平台代码：

```shell
make -C source/util/tests clean
make -C source/util/tests test -j8
```

链接主机侧测试时需要能通过 `KEYSTONE_PREFIX` 找到 Keystone，默认是 `/opt/homebrew`。

<br>

# 配置

HyperHEN 在下面两处读写同一份配置：

```text
/data/HyperHEN/config.ini
/user/data/HyperHEN/config.ini
```

大部分选项都可以在工具箱里改。配置格式是 `schema_version=1`。
如果还没有配置文件，HyperHEN 会按 [`config.ini.example`](config.ini.example)
写出一份带注释的默认文件。

| 配置项 | 默认值 | 可用值 |
| --- | --- | --- |
| `meta.schema_version` | `1` | `1` |
| `toolbox.language` | `system` | `system`, `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `it`, `es`, `pt-BR`, `pt-PT`, `pl`, `ru`, `ar`, `th`, `nl`, `fi`, `sv`, `da`, `no`, `tr`, `cs`, `hu`, `el`, `ro`, `vi`, `id`, `uk` |
| `startup.open_after_load` | `none` | `none`, `home_menu` |
| `home_screen.show_title_ids` | `false` | `true`, `false` |
| `game_menu.show_hyperhen_options` | `true` | `true`, `false` |
| `rest_mode.resume_reinject_delay_seconds` | `10` | `0` 到 `255` 秒 |
| `cheats.mirror` | `auto` | `auto`, `github`, `cnb` |
| `app_jailbreak.debug_notifications` | `false` | `true`, `false` |
| `cooling.fan_control` | `automatic` | `automatic`, `temperature_threshold` |
| `cooling.temperature_threshold_celsius` | `77` | `0` 到 `100` |
| `overlay.enabled` | `true` | `true`, `false` |
| `overlay.background` | `true` | `true`, `false` |
| `overlay.edge` | `top` | `top`, `bottom` |
| `overlay.align` | `center` | `left`, `center`, `right` |
| `overlay.font_size` | `medium` | `small`, `medium`, `large` |
| `overlay.order` | `fps,cpu,gpu,memory,ip,fan` | 逗号分隔的 `fps`、`cpu`、`gpu`、`memory`、`ip`、`fan` |
| `overlay.show_cpu` / `overlay.show_gpu` / `overlay.show_memory` / `overlay.show_fps` | `true` | `true`, `false` |
| `overlay.cpu_usage_mode` | `average` | `average`, `per_core` |
| `overlay.show_ip_address` | `false` | `true`, `false` |
| `shortcuts.cheats_menu` | `off` | `off`, `r3_l3`, `l2_triangle`, `long_options`, `long_share`, `share` |
| `shortcuts.toolbox` | `off` | `off`, `l2_r3`, `long_share`, `share` |

### 运行时数据

| 路径 | 用途 |
| --- | --- |
| `/data/HyperHEN/payloads/` | 用户 payload ELF |
| `/data/HyperHEN/plugins/` | 带 descriptor 的 HyperHEN 插件 ELF |
| `/data/HyperHEN/cheats/` | 金手指文件 |
| `/data/HyperHEN/cheats_tmp/` | HTTPS ZIP 与解压临时文件（同步后清理） |
| `/data/HyperHEN/HyperHEN.log` | 主运行日志 |
| `/data/HyperHEN/HyperHEN_crash.log` | 保留的 daemon 崩溃信号与回溯日志 |
| `/data/HyperHEN/HyperHEN_util_daemon.log` | Utility daemon 日志 |

<br>

# 主机侧工具

| 工具 | 用途 |
| --- | --- |
| [`scripts/daemon_log.py`](scripts/daemon_log.py) | 从主机拉取守护进程日志 |
| [`scripts/shutdown_hyperhen.py`](scripts/shutdown_hyperhen.py) | 关闭 HyperHEN 用户态进程，但不结束 `kstuff` |
| [`scripts/ps5_cmake.sh`](scripts/ps5_cmake.sh) | 通过 PS5 Payload SDK 运行 CMake |
| [`scripts/sync_dependencies.sh`](scripts/sync_dependencies.sh) | 获取或构建外部 payload 输入 |

<br>

# 仓库结构

```text
.
├── assets/                    项目图片资源
├── docs/                      架构与技术文档
├── scripts/                   构建、依赖和主机侧辅助脚本
├── source/                    本仓库的 PS5 源码
│   ├── bootstrapper/          启动与内嵌 payload 链
│   ├── daemon/                关键守护进程与工具箱注入
│   ├── util/                  工具守护进程、IPC 与金手指
│   ├── shellui/               工具箱与 ShellUI 挂钩
│   ├── i18n/                  共享的工具箱 / 通知多语言目录
│   ├── unpacker/              最终 HyperHEN payload 包装
│   ├── libonion_*/            本仓库共享库
│   ├── common/                共享底层实现
│   └── platform/ps5/stubs/    PS5 系统库链接 stub
├── third_party/               vendored 依赖与 source-pinned HyperCore
├── tools/                     仓库侧生成工具
└── build/                     生成的构建产物（忽略提交）
```

完整模块与 IPC 关系见[架构文档](docs/arch.md)。

<br>

# 故障排查

1. 加载 HyperHEN 前，确认外部 `elfldr` 能从 **9021** 连上。
2. 确认这份 payload 是用完整的 PS5 Payload SDK、按当前固件构建的。
3. 工具箱没出现时，等 `util → HyperCore → daemon` 启动完，再看日志。
4. HyperCore 构建失败时运行 `./scripts/sync_dependencies.sh`，它会报告缺失的 vendored source。
5. 报问题时请附上固件、漏洞加载端、SDK 版本、构建类型、日志和复现步骤。

<br>

# 参与贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

<br>

# 致谢

HyperHEN 离不开 PS5 自制软件与逆向工程社区。

### 贡献者

<p align="center">
  <a href="https://github.com/aydencharles/onionHEN/graphs/contributors">
    <img src="https://contrib.rocks/image?repo=aydencharles/onionHEN" alt="HyperHEN 贡献者"/>
  </a>
</p>

### 来源

- [OnionHEN](https://github.com/aydencharles/onionHEN) — 原始模块化架构；保留 GPLv3、版权声明及内部 `onion::*` 兼容 API
- [etaHEN](https://github.com/LightningMods/etaHEN) — LightningMods 与贡献者；本仓库的源码基线
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — SiSTR0 与贡献者；PS4 一体化 HEN，本项目沿这条路走

### 参考

- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch、sleirsgoevy 与贡献者；休息模式 Toolbox 恢复沿用其 SceSysCore `NOTE_EXEC` 监视 `NPXS40087`，并等待 `libSceNpTrophy.sprx` / `libSceNpTrophy2.sprx`
- [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager) — itsplk；休息后监听套接字重绑定（Unix IPC 与 TCP `accept` 失败自愈）参考了这个项目
- [HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection) — TeeKay87；内置金手指同步下载所用的社区金手指合集
- [PHU Games Tools](https://github.com/ArkSama) — ArkSama；游戏内 FPS 计数沿用 PHU Games Tools 的 skip-hook 采样（`/dev/dce` scanout 与对 `libSceAgcDriver` 的 DMAP 读取）

### 实际使用或内嵌

- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) — Prospero 工具链与头文件
- [elfldr](https://github.com/ps5-payload-dev/elfldr) — 端口 9021 的首次引导加载器；不打进 payload
- [kstuff-a53](https://github.com/Soonniicc/kstuff-a53) — 内嵌源码快照，将 kstuff-lite、A53/PPR 与 BackPork 合并为 HyperCore
- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch、sleirsgoevy 与贡献者；HyperCore 的基础
- [ppr-patch](https://github.com/drakmor/ppr-patch) — drakmor；A53/PPR 集成基础
- [BackPork](https://github.com/BestPig/BackPork) — BestPig；进程监控与 `fakelib` 挂载基础
- [a53-code-exec](https://github.com/cragson/a53-code-exec) — A53 技术参考；其 ELF 未被内嵌
- [onionHEN-ftpsrv-plugin](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin) — 基于 drakmor/ftpsrv 的可选外部 FTP 插件
- ShadowMountPlus — Drakmor 与贡献者；从源码构建并作为唯一的 HyperHEN Automount 引擎内嵌
- [libhijacker](https://github.com/astrelsky/libhijacker) — astrelsky；进程劫持与内核读写
- [NineS](https://github.com/buzzer-re/NineS) — buzzer-re；注入 ShellUI
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON 解析
- [7-Zip LZMA SDK](https://www.7-zip.org/sdk.html) — unpacker 解压
- [miniz](https://github.com/richgel999/miniz) — 金手指文件解压

### 测试人员

即食面、雨之声、大饼电玩、安定区、随风、麒麟、尼克库尔曼、云、啊烦、小小蔡、B站谢锡榆、荆枫、苏打先森、噢、天天向上

也感谢其他参与测试、研究和给出有效反馈的人。

<br>

# 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 发布。
第三方组件保留各自的许可证与声明。

> HyperHEN 是非官方自制项目，与 Sony Interactive Entertainment 无关。
> 请只在你自己的主机上使用，风险自负。本项目不提供任何担保。
