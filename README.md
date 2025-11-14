# YoursCraft
A game based on Minecraft and developed using OpenGL.
<br>
一个基于 OpenGL 的体素 (voxel) 游戏

> This project uses Minecraft texture resources (not included in this repository), which need to be available for download from the official website, for personal learning only, and commercial use is prohibited.
> <br>
> 本项目使用了 Minecraft 的贴图资源（不包含在本仓库中），需要可以去官网下载，仅用于个人学习，禁止商业用途

## Introduction / 简介

`Yourcraft` is a Minecraft style voxel rendering and interactive demonstration game based on OpenGL 3.3 Shader implementation. Mainly used for course assignments, including terrain generation, meshing, multi-threaded block generation, asynchronous texture loading, simple water simulation, and sphere physics.
<br>
`Yourscraft` 是一个 Minecraft 风格体素渲染与交互演示游戏，基于 OpenGL 3.3 Shader 实现。主要用于课程作业，包括地形生成、网格合并 (meshing)、多线程区块生成、异步纹理加载、简单水模拟与球体物理等功能。

## Functions / 功能

- Generate altitude map and biome terrain according to the program (Perlin noise) 
- 按程序生成高度图与生物群系的地形（Perlin 噪声）
- Use Greedy Meshing to merge block faces to reduce the number of vertices 
- 使用 Greedy Meshing 合并方块面以减少顶点数量
  Multi threaded terrain generation in the background and upload mesh data to the main thread (GL)
- 后台多线程生成地形并将网格数据上传到主线程（GL）
- Asynchronous texture loader (using SOIL), the main thread is responsible for GL upload
- 异步纹理加载器（使用SOIL），主线程负责 GL 上传
- Light and shadow
- 光线和阴影
- Simple water flow level simulation
- 简单水流层级模拟
- Can generate force bearing spheres (collision and simple elasticity/friction)
- 可生成受力球体（碰撞与简单弹性/摩擦）
- Block bar, day night cycle, camera gravity/flight mode
- 方块栏、昼夜循环、相机重力/飞行模式

## Control / 控制

- 移动：`W` `A` `S` `D`（水平），`Z` 上升，`X` 下降（或跳跃）
- 鼠标：移动视角（按 `TAB` 切换鼠标锁定）
- 左键：挖掘方块
- 右键：放置方块
- 滚轮：在快捷栏中切换方块
- 数字 1..7：直接选择方块类型
- `M`：切换运动模式（重力/飞行）
- `B`：在目标位置生成足球

## 实现要点

- 地形：使用多层 Perlin 噪声混合生成大陆（continent）、地形（terrain）、山脉（mountain）与细节（detail），并按高度与噪声决定方块类型与生物群系
- 区块系统：世界被拆分为固定尺寸的 `Chunk`，只在需要时创建。区块的网格可以在后台线程生成为 `MeshData`，然后主线程上传到 GPU
- Greedy Meshing：对每个面方向进行贪婪合并，合并相连且纹理相同的面以减少绘制三角形数量
- 裁剪：视锥体裁剪用于剔除不可见区块，，并仅绘制正方体可见的3个面，减少渲染负担
- 异步纹理加载：加载线程读取图片数据（SOIL），并将像素数据排队给主线程以进行 OpenGL 上传，减少渲染阻塞
- 渲染：不透明通道先绘制，透明面统一全局排序后从远到近绘制以保证正确混合；支持阴影贴图（depth map）用于软阴影近似
- 物理：球体使用简单刚体积分、逐帧碰撞检测（与方块）以及球-球间的弹性/摩擦处理，球表面纹理支持顶点 UV 变形以显示旋转

## 依赖

- C++14 编译器
- OpenGL 3.3 Core
- `GLFW`（窗口与输入）
- `GLAD`（加载 OpenGL 函数）
- `GLM`（数学库）
- `SOIL`（图片加载，可替换为 `stb_image`）
项目按常见的 Visual Studio/Windows 工程配置

## 安装与运行
#### 使用Windows安装包
下载运行release/OpenGLSetup1.msi，由于没有许可证安装过程中Windows Defender可能会阻止安装，选择“更多信息”->“仍要运行”即可。
#### 自行编译项目
1. 安装依赖（GLFW、GLAD、GLM、SOIL）并确保头文件与库路径正确
2. 使用 Visual Studio 打开工程文件（.vcxproj），或使用 CMake 生成构建工程
3. 编译（目标要求支持 OpenGL 3.3）
4. 运行可执行文件，程序会在启动时异步加载纹理并逐步进入主场景

## 项目结构

- `src/`：（`main.cpp`, `World.cpp`, `Chunk.cpp`, `Texture.cpp`, `Simulation.cpp`, `Camera.cpp`, `Shader.cpp` 等）
- `include/`：（`Common.h`, `Chunk.h`, `World.h`, `Texture.h`, `Shader.h`, `Simulation.h`）
- `minecraft_textures/`：请到 Minecraft 官网下载Java版贴图资源，版权归 Mojang/Microsoft 所有
