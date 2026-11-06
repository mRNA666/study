# TODO — 挖矿系统

> 最后更新：2026-11-06

---

## 1. 每挖一次卡顿 —— 已定位，待解决

### 实测数据

分段计时（2026-11-06，连续挖 8 次，8 线程并行，约 4.4 万三角形）：

| 阶段 | 耗时 | 占比 |
|---|---|---|
| `MC.Generate()` — SDF 求值 + 生成三角形 | **29 ms** | 16% |
| `FDynamicMesh3::Copy()` | 9 ms | 5% |
| `QuickComputeVertexNormals()` | 6 ms | 3% |
| `SetMesh()`（物理烘焙被 defer 时） | **1 ms** | 0.5% |
| **物理碰撞烘焙（PhysX trimesh）** | **139 ms** | **77%** |

**总计约 180 ms**，60 fps 的帧预算只有 16.7 ms。

### 根因

`UDynamicMeshComponent` 在网格变化时会调用 `InternalOnMeshUpdated()`：

```cpp
if (bDeferCollisionUpdates || bTransientDeferCollisionUpdates)
    InvalidatePhysicsData();
else
    RebuildPhysicsData();      // ← 用全部三角形同步烘焙 PhysX trimesh
```

每挖一次 = 把整块 4.4 万面的石头重新烘焙一遍物理网格，**在游戏线程同步执行**。

源码位置：`Engine/Source/Runtime/GeometryFramework/Private/Components/DynamicMeshComponent.cpp`

### 已经排除的假设（别重复试）

| 假设 | 结论 |
|---|---|
| "MC 是主因" | ❌ 只占 29 ms |
| "`EnableComplexAsSimpleCollision()` 那一行贵" | ❌ 它只是设标记，真正的烘焙被它触发或由网格变化触发 |
| "把碰撞配置挪到构造函数只设一次" | ❌ **会导致碰撞体根本不创建** —— 射线打不中、玩家穿过去 |
| "把烘焙关掉就快了" | ⚠️ 确实从 180 ms 降到 42 ms，但**碰撞彻底没了**，游戏不能玩 |

### 关键约束（踩过的坑）

**那四行碰撞设置必须在 `SetMesh()` 之后调用：**

```cpp
MeshComp->SetMesh(MoveTemp(Mesh));
MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
MeshComp->SetCollisionObjectType(ECC_WorldStatic);
MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
MeshComp->EnableComplexAsSimpleCollision();
```

放到构造函数里 → `UpdateCollision()` 时碰撞体没被创建 → 石头没有碰撞。

### 解决方案（A / B / C 可叠加）

| 方案 | 解决什么 | 预期效果 |
|---|---|---|
| **A. 射线改用 SDF 步进（raymarch）** | 瞄准不再依赖物理碰撞 | 挖洞响应立刻变快；还能精确命中挖出的坑壁 |
| **B. 碰撞网格降精度**（视觉 2 cm / 碰撞 8 cm） | 烘焙的三角形 4.4 万 → 约 2,700 | 烘焙 139 ms → **约 8 ms** |
| **C. 碰撞烘焙节流**（挖掘停止 0.4 s 后再烘） | 不在点击那一帧烘焙 | 点击那一帧 180 ms → 42 ms |

A + B + C 之后，点击路径只剩 `MC 29 + Copy 9 + Normals 6 ≈ 44 ms`。

### 实现要点

**A（SDF raymarch）**
- 从相机位置沿视线按固定步长前进，每一步求 `EvalSDF`，符号从正变负就说明穿过了表面
- 再用二分法细化到 1 mm 左右
- 只需要几十次 SDF 求值，比物理快几个数量级
- 好处：不需要石头有碰撞也能挖；洞挖出来之后射线仍然准确（物理 trimesh 会被简化）

**B（低精度碰撞网格）**
- `UDynamicMeshComponent` 的碰撞源就是它自己的 mesh，所以要另想办法：
  - 方案 1：加一个不可见的子组件专门当碰撞体（它的 mesh 用 8 cm 步长生成）
  - 方案 2：自己实现 `GetPhysicsTriMeshData`，返回低精度网格（需要查接口）
  - 方案 3：用 `SetSimpleCollisionShapes()` 塞几个盒体（精度最差，但烘焙几乎免费）

**C（烘焙节流）**
- 挖洞时：`SetDeferredCollisionUpdatesEnabled(true, false)` + `SetMesh()`（快）
- 同时启动一个 0.4 秒的 timer，每挖一次就重置
- timer 到期时再 `EnableComplexAsSimpleCollision()` / `UpdateCollision(true)`
- 代价：延迟期间物理是**旧形状**（玩家站在旧表面、射线打到旧表面），0.4 秒后追上

---

## 1.5 洞越多越慢（新发现，2026-11-06）

`EvalSDF` 里每个采样点都要遍历全部洞：

```cpp
for (const FDigHole& Hole : DigHoles)
{
    const double SphereDist = (P - Hole.Center).Length() - Hole.RadiusCm;
    Result = FMath::Max(Result, -SphereDist);
}
```

实测（同一块石头）：

| 洞数 | `MC.Generate()` |
|---|---|
| 2 | 29 ms |
| 44 | **50 ms** |
| 200（推算） | ~150 ms |

成本 = 采样点数 × 洞数。**这是会随游戏进行不断恶化的成本。**

**方向**：
1. **空间加速** —— 每个采样点只检查附近的洞。比如按 20 cm 网格把洞分桶，
   一个点只需查所在格及相邻格（通常 0~3 个洞）。
2. **合并小洞** —— 相邻的同半径洞合并成一个大球，减少数量。
3. **分块** —— 每个子块只保留落在它范围内的洞（配合分块重建一起做）。

---

## 2. 挖多次之后出现悬空的小碎块

**现象**：连续挖很多次后，石头上或空中残留一些孤立的小碎片。

**原因**：SDF 减法 `max(石头, -球)` 只负责"减掉球内的部分"。
如果某次挖洞把一小块石头和主体切断，那块就变成**孤立的壳**。
Marching Cubes 照样会生成它（它只认零等值面，不管连通性），
而 UE 不会自动把它变成可掉落的物体 —— 它就那么浮着。

**方向**：

1. **连通性检测** —— 重建后按三角形做连通分量分析，保留最大的一块，
   其余当成"掉落物"生成物理碎块。`FDynamicMesh3` 应该有组件分割的工具，需要查 API。
2. **先不管** —— 给它们加简单物理让它们掉下来，视觉上反而像挖下来的碎石，合理。

---

## 3. 下一步功能（未开始）

- [ ] 镐子等级 → 不同挖掘半径（见 `DESIGN-dig.md` 里的 tier 1/2/3 半径表）
- [ ] 矿脉：顶点色标出金矿，跟着矿脉挖
- [ ] 掉落物 + 背包 + 重量（50 kg 上限）
- [ ] 洗矿机

---

## 环境备忘（踩过的坑）

- **编译前必须关掉编辑器**，否则 `LNK1104`（DLL 被占用，链接器写不进去）
- **改了头文件 → Live Coding 打不了补丁**，必须完整编译（UHT 要重新生成反射表）
- **Live Coding 会骗人**：报 "no code changes detected" 不代表代码进去了，要看运行时行为
- 判断编译是否真的成功：`Result: Succeeded` + `Binaries/Win64/UnrealEditor-study.dll` 的时间戳
- `UDynamicMeshComponent` 默认是 `NoCollision`，必须显式打开
- **碰撞那四行必须在 `SetMesh()` 之后调用**，挪到构造函数会导致碰撞体不创建
- 复杂碰撞是逐三角形烘焙，石头多了会卡，最终要换简化碰撞
- 关卡里那块石头保存了 `DigHoles`（测试时加的洞），改代码后记得在细节面板清空
- 编辑 `DigVolume.*` 这类含中文的文件后，要**确认 UTF-8 BOM 还在**（编辑工具会吃掉它）
- `studyCharacter.cpp` 保持纯 ASCII（英文注释），避免 BOM 问题
