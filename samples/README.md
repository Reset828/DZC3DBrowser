# samples/

示例资产，用于验证 glTF 2.0 导入（task 1.2）。两个文件都是**自包含**的，不依赖任何外部文件。

| 文件 | 说明 |
|------|------|
| `cube.gltf` | 纯 JSON，buffer 与图像都以 base64 data URI 内嵌 |
| `cube.glb` | 二进制容器，JSON 块 + BIN 块（buffer 无 uri） |
| `multi_material.obj` + `multi_material.mtl` + `checker.png` | 多材质示例（task 1.4）：一个 OBJ 含 3 个立方体，分别使用纹理材质、纯色材质、半透明材质 |

两者内容等价：一个两层节点层级（根节点 → 立方体节点），一个带基础色与基础色纹理的材质，一张内嵌 PNG 图像，顶点含 POSITION / NORMAL / TEXCOORD_0（无 COLOR_0）。

按 1.2 的范围，纹理只解析声明与来源；1.3 起会解码被材质引用的图像并上传为 GPU 纹理；1.4 起基础色纹理会在片元着色器中采样（baseColor × 顶点色 × 纹理）。顶点无 COLOR_0 时颜色为白，导入后消息区会输出一行纹理汇总（sRGB 颜色纹理 + mip 级数）。

`multi_material.obj`（task 1.4）用于验证材质与 SubMesh 渲染链路：三个立方体各用一个材质（`map_Kd` 纹理 / 纯色 `Kd` / `d 0.45` 半透明），可在同屏看到至少两种不同材质，并通过工具栏“材质”面板单独开关每个材质的可见性。
