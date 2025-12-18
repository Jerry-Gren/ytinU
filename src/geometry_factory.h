#pragma once

#include <vector>
#include <memory>
#include "base/vertex.h"
#include "model.h" // 我们需要返回 Model 对象

class GeometryFactory
{
public:
    // 1. 立方体 (用于墙壁、地板、箱子)
    static std::shared_ptr<Model> createCube(float size = 1.0f);

    // 5. 平面 (专门用于地板，虽然可以用压扁的立方体代替，但单面更高效)
    static std::shared_ptr<Model> createPlane(float width = 10.0f, float depth = 10.0f);

    // 2. 球体 (用于装饰、测试光照)
    // stacks: 纬度切片数, slices: 经度切片数 (越高越圆)
    static std::shared_ptr<Model> createSphere(float radius = 0.5f, int stacks = 16, int slices = 32);

    // -----------------------------------------------------------------------
    // 通用几何生成核心 (Frustum)
    // -----------------------------------------------------------------------
    // 这是一个万能函数：
    // - topRadius == bottomRadius, slices > 20 -> 圆柱
    // - topRadius == 0                         -> 圆锥
    // - topRadius != bottomRadius              -> 圆台
    // - slices == 3, 4, 5, 6...                -> 三棱柱/台, 四棱柱/台...
    static std::shared_ptr<Model> createFrustum(float topRadius, float bottomRadius, float height, int slices, bool useFlatShade);

    // 4. 圆柱体 (Cylinder) - 实际上是调用 createFrustum
    static std::shared_ptr<Model> createCylinder(float radius = 0.5f, float height = 1.0f, int slices = 32, bool useFlatShade = false);

    // 5. 圆锥体 (Cone) - 实际上是调用 createFrustum
    static std::shared_ptr<Model> createCone(float radius = 0.5f, float height = 1.0f, int slices = 32, bool useFlatShade = false);

    // 6. 多面棱柱 (Prism) - 比如六棱柱: radius=1, slices=6
    static std::shared_ptr<Model> createPrism(float radius = 0.5f, float height = 1.0f, int sides = 6, bool useFlatShade = false);

    // 7. 多面棱台 (Prism Frustum) - 比如四棱台: topR=0.5, bottomR=1, slices=4
    static std::shared_ptr<Model> createPyramidFrustum(float topRadius, float bottomRadius, float height, int sides = 4, bool useFlatShade = false);
};