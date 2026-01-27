---
name: tests
description: 创建测试用例时，以及在提交代码之前运行测试用例时使用。
---

# 日志要求

功能XXX的测试用例的日志，需要使用[XXX test]的前缀。

# 内核源文件

所有测试用例的内核代码放在tests/kernel目录下。

#测试用例

运行脚本放在tests/scripts目录下，基于内核日志输出判断测试用例是否通过。
测试结束后，用pkill -9 qemu-system-x86的方法清理所有残留进程。
