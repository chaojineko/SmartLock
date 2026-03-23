# STM32F407 FreeRTOS 智能门锁

基于 STM32F407VET6 开发板、CubeMX 和 FreeRTOS 的智能门锁项目。

## 项目简介

本项目实现了多种本地身份认证方式，用于门锁控制场景，包含键盘密码、指纹和 RFID 三种开锁方式。

## 已实现功能

- 密码管理与开门（增删查）
- 指纹管理与开门（增删查）
- RFID 卡管理与开门（增删查）

## 开发环境

- STM32CubeMX（用于外设与引脚配置）
- Keil MDK 5.43
- ARM Compiler 5.06
- ST-Link（下载与调试）

## 快速开始

1. 安装 Keil MDK 5.43，并确保已安装 ARM Compiler 5.06。
2. 打开工程文件：`MDK-ARM/projectfreertos.uvprojx`。
3. 连接开发板和 ST-Link。
4. 在 Keil 中编译并点击下载按钮烧录程序。

## 工程说明

- `projectfreertos.ioc`：CubeMX 工程配置，可查看引脚和外设连接。
- `Core/`：用户代码与应用逻辑（任务、驱动封装、业务逻辑）。
- `Drivers/`：HAL 驱动与 CMSIS。
- `Middlewares/Third_Party/`：FreeRTOS 等中间件代码。
- `MDK-ARM/`：Keil 工程与构建输出。

## 计划功能（TODO）

- ESP32-CAM 接入
- 人脸识别
- 二维码临时授权设计
- 上云能力
- 小程序 / Android App 开发

## 说明

本项目当前主要面向本地离线门锁控制场景，后续会逐步补充联网与远程管理能力。
