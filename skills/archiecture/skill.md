---
name: 架构
description: 设计规划新功能时。
---

# 公共功能要求

1. 支持SMP，公共的临界区需要使用锁保护。
2. 体系结构相关的代码放在arch/${ARCH}下面，头文件放在arch/${ARCH}/include/下面。
3. 体系结构无关的代码放在kernel/${SUBSYSTEM}下面，头文件放在include目录下面。
