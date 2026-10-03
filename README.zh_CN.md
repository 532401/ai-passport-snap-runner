**简体中文** · [English](README.md)

# 响指跃行

运行在 **FoloToy AI Passport** 上的离线响指跑酷游戏。像素精灵在夜景中奔跑，
打响指跳过石块和坑；麦克风测量响指力度，映射为十档跳跃高度。

基于 [AI Passport 官方项目](https://gitee.com/FoloToy/ai-passport)，复用 BSP，
重新设计中文应用界面。硬件为 ESP32-C3、8 MB Flash、无 PSRAM、240 × 320 屏幕。
不需要联网，不播放扬声器声音。

## 玩法

1. 在安静环境中，让手与麦克风保持固定距离。
2. 首次使用按确定，打三次强响指，以其中最轻的一次作为 Max。
3. 确认后打三次轻响指，以平均值作为 Min。
4. 在练习页试跳，再按确定开始计分跑局。

校准区间按对数分为十档，响指越强跳得越高；空中不能连跳。
三次受伤后结束，奔跑距离即分数；校准和已结束跑局的最高纪录保存在本机。

前 1000 游戏米逐步增加障碍密度，随后封顶。最小间隔始终保留最高速度下的
完整最大跳跃时间及 350 ms 余量，确保存在适时强跳的可通过路线。
力度不足或时机错误仍会受伤。识别采用瞬态启发式算法，拍手、敲击也可能触发。

| 页面 | 上键 | 下键 | 确定 |
| --- | --- | --- | --- |
| 已校准首页 | 练习 | 重新校准 | 开跑 |
| 校准 | 取消 | 无操作 | 两段采集之间确认 |
| 练习／游戏结束 | 首页 | 重新校准 | 新跑局 |
| 奔跑中 | 无操作 | 无操作 | 暂停 |
| 暂停 | 无操作 | 首页 | 继续 |

校准规则、全部按键、模型细节及设备验收清单见[应用指南](docs/apps/snap-runner.zh_CN.md)。

## 构建

先按[环境指南](docs/development/engineering/environment-setup.zh_CN.md)安装并激活
**ESP-IDF 5.5.3**，再运行：

```sh
git clone --branch feature/custom-passport-app https://github.com/532401/ai-passport-snap-runner.git
cd ai-passport-snap-runner
./tools/validate.sh --static
./tools/validate.sh
```

完整门禁验证主机逻辑、字体和固件布局，生成 `build/FoloToy-AI-Passport-full.bin`，
并将匹配的 ELF／MAP 归档到 `build/firmware/<sha256>/`。
从 `0x0` 刷入验证后的**合并镜像**，不要在此地址刷入应用单镜像。
合并刷写会重置校准和纪录，操作遵循[刷机说明](docs/development/engineering/firmware-layout.zh_CN.md)。

## 验证状态

- Build：PASS，ESP-IDF 5.5.3 构建和合并镜像验证。
- Host tests：PASS，校准、检测、物理、会话流程、120 个随机种子地图、144 组最小
  间隔组合，以及内存／未定义行为检测和字体检查。
- Device tests：PASS，仅限密度版本刷写及 45 秒启动日志窗口，未发现看门狗或崩溃诊断。
- Unverified：真人响指准确率、实际屏幕／操作验收、长时间游玩、密度手感、断电保存、
  睡眠和电流表现。

实测固件的准确身份及记录见[应用指南](docs/apps/snap-runner.zh_CN.md)。
构建产物和原始设备日志不进入 Git。游戏位于 `feature/custom-passport-app` 分支；
`main` 保留上游基线。

## 许可与来源

响指跃行的原创新增内容及修改采用 [Apache License 2.0](LICENSE)。
继承的 AI Passport 代码保留 [MIT 许可](LICENSES/AI-Passport-MIT.txt)和 FoloToy 署名；
适用范围及上游来源见 [NOTICE](NOTICE)。Noto Sans SC 及生成的字体子集继续使用
[SIL Open Font License](assets/fonts/OFL.txt)，来源见[资源文档](assets/README.zh_CN.md)。
ESP-IDF 和托管依赖保留各自许可证。

Apache-2.0 允许商用、修改和再分发（包括用于闭源产品），需遵守许可证及声明保留要求。
它包含贡献者明确的专利授权，不要求下游公开修改后的代码。
完整条款以[官方许可证](https://www.apache.org/licenses/LICENSE-2.0)为准。
