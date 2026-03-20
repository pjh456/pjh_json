# 零拷贝与所有权模型

本文说明 `pjh_json` 如何实现零拷贝字符串，以及调用方必须遵守的所有权规则。

## 核心思路
解析后的字符串以 `std::string_view` 形式直接引用输入 buffer，
从而避免：
- 每个字符串的分配
- 字符数据的重复拷贝
- 额外的生命周期管理开销

代价是：**buffer 必须在所有 `Json`/`string_view` 使用期间保持存活**。

## Document 作为唯一所有者
解析函数返回 `Document`，它：
- 继承 `Json`，代表根节点
- 持有 `std::pmr::string buffer`

该 buffer 是字符串数据的唯一来源，`Document` 存活期间
`Json::as_string()` 返回的 `string_view` 都安全可用。

## 解析入口与所有权
三种解析方式：

1. `parse_copy(json_text, res)`
   - 复制输入文本到新的 PMR string
   - 追加 64 字节 `\0` padding
   - 返回拥有 buffer 的 `Document`

2. `parse_in_situ(buffer, res)`
   - 接管调用方提供的 PMR string
   - 要求 buffer 已包含 64 字节 padding
   - 会原地修改内容（转义会被重写）

3. `parse_file(path, res)`
   - 读取文件到 PMR string 并追加 padding
   - 返回拥有 buffer 的 `Document`

## 字符串生命周期规则
`Json` 的字符串是 `std::string_view`，因此：
- 没有分配，速度快
- `Document` 析构后视图失效

调用方规则：
- 不要在 `Document` 生命周期之外保存 `string_view` 或 `Json` 引用
- 若需要长期保存，主动复制为 `std::string`

## Object Key 同样是视图
`Object::Entry` 为 `std::pair<std::string_view, Json>`，
键也引用同一 buffer，生命周期规则完全一致。

## 修改与安全
由于字符串指向原始 buffer：
- 解析后不应修改 buffer
- 不应在同一 buffer 上重复解析

总结：`Document` 统一拥有内存，所有字符串都是借用视图，
这是性能优先的核心设计决策。
