# 对象与数组布局

本文说明数组与对象的内存结构，以及访问语义背后的设计取舍。

## Array 布局
`Array` 本质是：
- `std::pmr::vector<Json>`

设计选择：
- 连续存储，遍历局部性好
- PMR 分配，支持统一内存策略

访问方式：
- `operator[](size_t)` 不做越界检查
- `at(size_t)` 越界抛异常

## Object 布局
`Object` 实现为：
- `std::pmr::vector<std::pair<std::string_view, Json>>`

设计选择：
- key 为 `string_view`，零拷贝
- vector 存储，迭代快、结构紧凑
- 线性查找，避免哈希开销

访问语义：
- `operator[](key)` 若不存在则插入 `Json()`
- `operator[](key) const` 不存在则抛异常
- `at(key)` 不存在则抛异常

## 取舍
优点：
- 结构简单、内存占用低
- 遍历性能优
- 缓存友好

缺点：
- key 查找为 O(n)

该设计假设多数对象规模中小，
优先保证解析吞吐与内存局部性。
