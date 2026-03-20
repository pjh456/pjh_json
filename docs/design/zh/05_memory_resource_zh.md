# 内存资源（PMR）设计

本文说明 `pjh_json` 如何使用 `std::pmr` 来控制分配行为与回收成本。

## 为什么选择 PMR
JSON 解析会产生大量小对象：
- 数组与对象节点
- vector 扩容
- 解析过程中的临时内存

PMR 能提供：
- 可控的内存策略
- 统一的生命周期管理
- 低碎片与高局部性

## 核心容器
- `Array::Vec` 是 `std::pmr::vector<Json>`
- `Object::Vec` 是 `std::pmr::vector<std::pair<std::string_view, Json>>`

`Array`/`Object` 均保存资源指针，并用于内部 `Impl` 的分配。

## 资源传递路径
解析器接受可选的 `std::pmr::memory_resource*`：
- `Parser` 内部保存该指针
- `Array`/`Object` 构造时使用该资源
- `parse_copy`/`parse_in_situ`/`parse_file` 都支持传入资源

调用方可以一处控制整个 DOM 的分配方式。

## 高性能常见配置
基准测试中常用两层资源：
- `std::pmr::monotonic_buffer_resource` 绑定大块连续 buffer
- 上层包一层 `std::pmr::unsynchronized_pool_resource`

收益：
- 解析结束后 O(1) 回收
- 小对象分配更高效
- 内存碎片显著减少

## Array/Object 的分配方式
`Array`/`Object` 使用 PIMPL：
- `Impl` 保存 `pmr::vector`
- `Impl` 本身也从同一资源分配

这样能让相关数据更紧凑，提升缓存友好性。

## 设计权衡
PMR 会带来：
- 对象尺寸略增（资源指针）
- 构造逻辑稍复杂

但它换来的是解析时可控、可预期的内存行为，
这对于高吞吐 JSON 解析至关重要。
