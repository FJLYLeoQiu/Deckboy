# 来源与署名

This is the Chinese text of [NOTICE.md](NOTICE.md).

## 本仓库不是官方 Deckboy 发行版

本仓库是 [Deckboy](https://github.com/Utopian-Academy/Deckboy) 的**非官方简体中文本地化**，供私人使用。它**不是**原作者或 Utopian Academy 发布的版本，不得被表述为官方构建、官方分支声明或原项目的产品。

## 上游来源

- 原项目：<https://github.com/Utopian-Academy/Deckboy>
- 上游版本：**0.99.427**
- 本汉化所依据的精确提交：
  [`5da84439b112bca9461d20b71f0e46e4e4b733aa`](https://github.com/Utopian-Academy/Deckboy/commit/5da84439b112bca9461d20b71f0e46e4e4b733aa)

本仓库保留直至该提交的上游 git 历史，并在其上增加汉化与署名提交。

## 版权与许可

Deckboy 的版权归 Deckboy Contributors 所有（Copyright © 2026 Deckboy Contributors）。

本仓库中的 `LICENSE` 文件是未经改动的 **GNU 通用公共许可证第 3 版（2007 年 6 月 29 日）** 全文。上游 README 将项目表述为采用 **GNU GPL v3.0 或更高版本**。捆绑的第三方组件保留各自许可证，详见上游 README。

本次非官方汉化**不更改**该许可证，**不删除或改写**原版权与作者署名，也**不声称**拥有原程序的作者身份。

## 这里改了什么

- 在项目既有国际化机制上，扩充并修正了简体中文界面词库 `data/lang/zh-Hans.tsv`。默认语言仍是英语；在**设置 → Language** 中选择「简体中文」。
- 增加了中文安装与使用文档（`README.zh-Hans.md`、`docs/zh-Hans/`）。
- 为手机遥控器增加了可选的简体中文标签（`data/web/remote.html`、`data/web/remote.js`）。
- 增加了本说明，以及一份仅用于私人仓库、不会发布 Release 的 Windows 打包工作流。

TAKE、PROGRAM、NDI、SRT、OSC 等行业用语按上游惯例保留英文。命令名和远程协议未改。

## 汉化当时实际做过的检查

以下检查于 2026-11-13 实际运行：

- `python3 tools/audit_i18n.py --gate` 通过。
- `python3 tools/audit_i18n.py` 显示 `zh-Hans` 官方覆盖率约 **99.7%**（2346 / 2353）。余下缺口是审计工具未还原 C 字符串 `\xNN` 转义造成的假缺失（例如词库中已有的 `NDI® NETWORK`）。按还原后的运行时字符串计算为 **100%**（2226 / 2226）。
- 词库校验：无重复键、无空译、`{n}` / `{1}` 占位符完整。
- `node --check data/web/remote.js` 通过。

当时未验证：

- 未能在汉化用的机器上编译 `deckboy-i18n-check` 或完整应用程序。
- 未在真实显示器上目视检查桌面界面。
- 未运行 `tools/check_phone_remote.py`（需要已编译的 Deckboy）。
- 此后若产出 Windows 程序包，除非另行记录了在 Windows 电脑上的桌面运行，否则只应视为**编译 / CI 自检**结果，不能当作已完成实地运行验证。详见 `docs/zh-Hans/使用说明.md`。

NDI® 是 Vizrt NDI AB 的注册商标。
