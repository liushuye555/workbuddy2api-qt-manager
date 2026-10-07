# WorkBuddy2API Qt Manager

面向 Windows 的 WorkBuddy2API 本地桌面管理器，使用 Qt Widgets 编写。它为已有的 WorkBuddy2API 部署提供图形化操作入口；**它不是 API 网关本身，也不包含网关服务、账号凭证或你的本地配置**。

> 本项目是社区工具，与腾讯及 WorkBuddy / CodeBuddy 官方无关。请只使用你本人拥有或获授权的账号，并自行确认上游平台规则。

## 功能

- **服务管理**：查看本地网关状态，启动、停止、重启服务，打开验证页面和日志目录。
- **账号管理**：查看账号池状态，打开登录验证，导入凭证 JSON，重新登录、启用、禁用或删除账号。
- **模型目录**：查看 CN / Global 模型，搜索并复制模型 ID。
- **用量统计**：查看网关已记录的请求数、成功率、Token 与 Credit 汇总，按时间范围筛选，并导出 JSON / CSV。
- **任务中心**：手动运行签到、积分日报和 Global Trial；查看并执行签到、活跃上报、猫猫旅行、Token 保活、开学季及夜猫子任务；查看任务开关、计划和执行历史。
- **聊天测试**：通过本地网关发送 Chat Completions 测试请求；请求会实际访问上游并可能消耗账号额度。
- **可视化设置**：编辑监听地址、API Key、任务计划、Global / CN、提示词、上游请求参数、账号池、Redis 状态镜像等配置。
- **日志与诊断**：查看运行日志并导出脱敏诊断报告。
- **Windows 桌面体验**：系统托盘、重复启动时唤醒已有窗口、可选登录启动和故障恢复设置。

## 运行要求

- Windows 10 / 11，64 位。
- 一个已部署并配置好的 [WorkBuddy2API](https://github.com/Sliverkiss/workbuddy2api) 项目目录。
- 管理器需要能在该目录中找到 `config.json`、`start.ps1` 和 `stop.ps1`。启动服务还需要 `wb2api.exe`；登录与部分手动任务需要项目中的脚本文件。
- 登录和 `.sh` 任务需要安装 Git for Windows（Git Bash），并确保其 Bash 可用。

管理器会从程序所在目录和当前目录向上查找 WorkBuddy2API 项目目录。最简单的安装方式是把 Release 压缩包解压到已有项目根目录；不要覆盖自己的 `config.json`、`auths/` 或 `data/`。

## 下载与使用

1. 从本仓库的 **Releases** 下载 Windows x64 压缩包。
2. 将压缩包内容解压到已有的 WorkBuddy2API 项目根目录，并保留 Qt 运行库和 `platforms/` 等目录结构。
3. 双击 `workbuddy2api-manager.exe`。如服务未运行，可在“概览”页点击“启动服务”。
4. 在“账号”页点击“打开验证”完成登录，或点击“导入 JSON”添加已有凭证；导入后按提示重启网关使新账号生效。
5. 从“任务中心”“用量统计”“模型目录”和“聊天测试”使用其他功能。

关闭主窗口默认会隐藏到系统托盘。若要结束管理器并停止它管理的网关服务，请在托盘菜单中选择“退出并停止服务”。

### 用量统计说明

统计数据来自本地网关的 `/usage` 接口和本地保存的数据。管理器不会向上游补查历史调用；网关此前未记录的数据无法由本工具还原。清空统计会删除本地统计记录，操作前请先导出备份。

### 任务与聊天测试提示

签到、积分查询、Global Trial、活跃上报、猫猫旅行、Token 保活及活动任务会访问上游；部分操作可能产生账号状态变化或消耗额度。聊天测试也会实际调用模型。请先确认账号、区域和任务设置，再执行。

## 从源码构建

需要 Qt 6（包含 Qt Widgets、Qt Network）以及与 Qt 安装匹配的 MinGW 64 位工具链。打开对应的 Qt MinGW 命令行，在仓库根目录执行：

```powershell
cd manager
qmake manager.pro
mingw32-make release
```

程序会生成在仓库根目录。要制作可拷贝的 Windows 运行包，可使用同一 Qt 安装中的 `windeployqt` 部署运行库，并将生成的程序、`assets/workbuddy2api-manager.ico` 和 Qt 插件目录一并打包：

```powershell
windeployqt --release ..\workbuddy2api-manager.exe
```

发布包只应包含管理器及其运行依赖，不应包含上游服务配置、账号凭证、API Key、Device Token、Redis Token、日志、统计数据或个人备份。

## 安全与隐私

- `auths/` 中的账号 JSON 含敏感凭证。不要提交到 GitHub、聊天群或 Issue，也不要把它放进公开 Release。
- `config.json` 可能包含 API Key、Device Token、Redis Token 等秘密；公开分享前先检查并脱敏。
- 诊断报告和日志也应在分享前检查，避免包含个人路径、账号信息或服务细节。
- API Key、账号凭证和上游 Token 应按密码保护；只在可信的本机环境中使用管理器。

## 问题反馈

请在 [Issues](https://github.com/liushuye555/workbuddy2api-qt-manager/issues) 提交问题。反馈前请注明 Windows 版本、管理器版本和复现步骤；不要附上真实 Token、账号 JSON 或未脱敏日志。

## 许可证与再分发限制

本仓库中由本项目作者提供的管理器源码、图标及据此构建的管理器程序，采用仓库内的自定义《个人学习与研究许可 1.0》。该许可仅授权个人进行非商业学习、研究和评估；允许为本人使用在个人设备上下载、保存必要副本及修改，但**禁止将管理器源码、管理器二进制或其修改版本再分发、转交、镜像、公开发布、转售或提供给其他人**。需要超出许可范围的使用，请先取得作者书面授权。

此限制只适用于本项目管理器自有部分，不覆盖 WorkBuddy2API 上游项目及 Qt、MinGW 等第三方组件；第三方组件仍按各自许可证行使权利。详见 [第三方组件声明](THIRD-PARTY-NOTICES.md) 和发布包内的 `licenses/` 目录。

本项目虽然公开源码供查看，但使用自定义限制性许可，不是 OSI 定义的开源软件。

此为自定义许可文本，不构成法律意见；如需将其用于正式许可或维权，请按适用法律咨询专业律师。

**历史版本说明：** `v0.1.0` 最初以 PolyForm Noncommercial License 1.0.0 发布。该版本当时授予的权利仍按该版本随附的许可证处理；本自定义许可适用于 `v0.1.1` 及之后按新许可发布的版本，不能追溯改变既有授权。
