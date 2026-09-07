# logs/ — AI Coding 日志目录

存放你在开发中与 AI 工具的对话日志，和作品代码一并提交。

## 本队伍实际内容

- 本队全程使用**豆包（Doubao）**桌面端 AI 助手完成开发（BSP 移植、驱动编写、串口排障、实验迭代）。
- 豆包不在官方采集器支持的工具列表（Claude Code / OpenCode / Codex / AIoT-IDE）内，因此**没有采集器自动导出的 JSONL 会话原文**。
- 本目录提供人工整理的开发过程摘要：`QQQuer/development-notes.md`（时间线、各模块排障要点、AI 使用说明、合规说明）。
- 后续如使用官方支持工具在 openvela 工作区开发，日志将由采集器自动写入本目录（格式：`<github_login>/<date>/<tool>__<sid>.jsonl`）。

## 官方模板说明（保留）

```text
logs/
└── <github_login>/              # 你的 GitHub 用户名，一人一目录
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件（工具名与 session id 用 __ 连接）
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，由组委会提供的日志归集工具导出，**只提交 JSONL 本身**。

导出与提交的完整步骤、字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
