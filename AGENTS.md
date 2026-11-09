# 项目维护约定

- 用户要求每一次修改和改进都记录版本日志。
- 修改播放器代码或配置时，同步更新 outputs/版本更新日志.md，记录版本、原因、改动、需更新端、验证结果和已知问题。
- 区分编译、语法检查、用户上板实测，未实测不得宣称已解决。
- 当前用户确认正常的基线为 v20.9（2026-11-03）。
- 版本化 Keil 工程文件共用源码；做可回退备份时保留完整源码，不能仅复制 uvprojx。
- 不将 Wi-Fi 密码、登录令牌或用户会话写入版本日志。

- 历史触屏版本 v22.0：outputs/STM32_Touch_Console 与 outputs/ESP32_Touch_Console，用户实测有定位/触摸/本地加载/歌词问题；不要覆盖 v20.9 或 v21 源码。

- v23.0 为最新修复版：outputs/STM32_Touch_Fix 与 outputs/ESP32_Touch_Fix；首次四点校准保存在片内最后2KB，链接程序区0x08000000/0x7F800；按下反馈、启动TF恢复、快速近似定位及HTTP/1.1歌词请求待上板验收。

- v24.0 当前待验收修复：outputs/STM32_Playback_Fix / ESP32_Playback_Fix，工程 Player_v24.uvprojx，两端握手24，校准数据仍兼容23。v23用户反馈本地卡顿、歌词请求失败；v24减轻采样计算与界面等待，新增歌词具体错误诊断，未获实机验证不得宣称解决。

- v25.0 待验收：ESP32_Interaction_Fix / STM32_Interaction_Fix，Player_v25.uvprojx，双端握手25，校准仍兼容。修复分类/键盘/音量交互与TLS X509_ALLOC_FAILED，MP3原文件下载DM:length协议必须双端更新；构建及实测状态见版本日志。

- v26.0 当前无线升级版：ESP32_OTA_Player / STM32_OTA_Player / STM32_OTA_Bootloader，首次有线安装。STM应用起址0x08008000、长度0x77800，boot代码<0x7000，identity0x7000，marker页0x7800，校准0x7F800；不要用旧版0x08000000应用替换。ESP自定义CSV双6MB程序槽。维护版本日志与Wireless_Update_v26说明；无实机证据不得宣称OTA断电恢复已验收。

- v27.0 曲库/本地定位修复：ESP32_Library_Fix、STM32_Library_Fix，Player_v27.uvprojx，OTA包与握手仍兼容26，boot/分区不动。真实故障为本地仅1首与本地进度条定位约5秒；加入原子列表/数量报告/8KB接收、清筛选、句柄复用及0.5秒预热，实机待验证。
