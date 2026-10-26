# 按仿真时刻读取刚体状态

这是尚未发布的源码能力。新增接口改变原生 ABI；Core、状态提供者、Sensor 和 ROS 必须一起重新编译，不能混用旧 DLL。已有资产和最新状态接口保持可用。

## 职责

Core 只定义契约。提供者实现 `IUBotBodyStateSource::AcquireBodyStateHistory(Body)`，返回 `TSharedPtr<const IUBotBodyStateHistory>`；默认实现返回空，表示不支持历史。`GetBodyState` 继续返回最新物理状态。

历史句柄只读、只在游戏线程使用。它请求提供者记录状态，不持有 Actor、组件或 World 的强引用。多个消费者可以共享一份存储；最后一个句柄释放后，提供者可以停止记录。提供者必须在所属对象退出时使存活句柄失效。

## 查询

- `GetRange` 返回有限的有效区间和 `Generation`。时间线被清空时，代数变化。
- `Query(Time, State, Segment)` 返回 Exact、Interpolated、NotReady 或 Unavailable。
- 只有成功结果会写出状态和连续段标识。时间戳表示被查询的仿真时刻，单位和坐标约定沿用 `FUBotBodyState`。
- 不允许外推，也不允许跨瞬移、物理跳步或碰撞冲量等不连续边界插值。插值算法、保留窗口和存储属于提供者。
- 消费者应区分等待尚未完成的物理状态、已过期历史和不连续区间，不能把最新状态重新标记为多个历史样本。

历史只描述所提供刚体的运动。它不代表历史碰撞场景、环境介质、可变挂载或渲染结果，也不保证在墙钟上均匀发送消息。
