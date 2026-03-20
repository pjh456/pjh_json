# 设计文档总览

本目录包含 `pjh_json` 的一系列技术设计文档，重点解释性能与易用性之间的取舍，
并给出实现层面的关键细节。

建议阅读顺序：
1. `00_overview_zh.md` - 本文
2. `01_simd_optimization_zh.md` - SIMD 使用与 padding 策略
3. `02_zero_copy_ownership_zh.md` - 零拷贝模型与所有权规则
4. `03_inplace_construction_zh.md` - 原地构造与覆盖写策略
5. `04_fast_slow_paths_zh.md` - 快慢路径划分与触发条件
6. `05_memory_resource_zh.md` - PMR 设计与分配策略
7. `06_string_parsing_zh.md` - 字符串解析、转义与 Unicode
8. `07_number_and_literal_zh.md` - 数字与字面量解析
9. `08_object_array_layout_zh.md` - 容器布局与访问语义

范围与不变式：
- 解析器是 DOM 模型，返回持有底层 buffer 的 `Document`。
- 字符串以 `std::string_view` 形式引用 buffer。
- `Array`/`Object` 使用 `std::pmr` 便于自定义内存资源。
- 快路径尽可能短且稳定，慢路径保证正确性。

如果需要新增文档，下一步可以补一份“序列化设计”说明。
