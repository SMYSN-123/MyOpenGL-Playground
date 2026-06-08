#ifndef FRUSTUM_CULLING_H
#define FRUSTUM_CULLING_H

#include <glm.hpp>
#include <Camera.h>

// 1. 定义无限延伸的平面
struct Plane
{
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    Plane() = default;

    // 用平面上的一个点和法线来构造平面
    Plane(const glm::vec3& p1, const glm::vec3& norm) : normal(glm::normalize(norm)), distance(glm::dot(normal, p1))
    {

    }

    // 获取空间中任意一点到平面的有向距离
    float getSignedDistanceToPlane(const glm::vec3& point) const
    {
        return glm::dot(normal, point) - distance;
    }
};

// 2. 定义视锥体（由摄像机的 6 个裁剪平面组成）
struct Frustum
{
    Plane topFace;
    Plane bottomFace;
    Plane rightFace;
    Plane leftFace;
    Plane farFace;
    Plane nearFace;
};

// 🌟 3. 核心生成器：根据相机实时生成 6 面墙 (使用修复后的正向逻辑)
inline Frustum createFrustumFromCamera(const Camera& cam, float aspect, float fovY, float zNear, float zFar)
{
    Frustum frustum;
    const float halfVSide = zFar * tanf(fovY * .5f);
    const float halfHSide = halfVSide * aspect;
    const glm::vec3 frontMultFar = zFar * cam.Front;

    // 前后
    frustum.nearFace = { cam.Position + zNear * cam.Front, cam.Front };
    frustum.farFace = { cam.Position + frontMultFar, -cam.Front };
    
    // 右左 (修正版逻辑：找右边缘算右面，找左边缘算左面)
    glm::vec3 rightEdge = frontMultFar + cam.Right * halfHSide;
    frustum.rightFace = { cam.Position, glm::cross(cam.Up, rightEdge) };
    
    glm::vec3 leftEdge = frontMultFar - cam.Right * halfHSide;
    frustum.leftFace = { cam.Position, glm::cross(leftEdge, cam.Up) };
    
    // 上下
    glm::vec3 topEdge = frontMultFar + cam.Up * halfVSide;
    frustum.topFace = { cam.Position, glm::cross(topEdge, cam.Right) };
    
    glm::vec3 bottomEdge = frontMultFar - cam.Up * halfVSide;
    frustum.bottomFace = { cam.Position, glm::cross(cam.Right, bottomEdge) };

    return frustum;
}

// 🌟 新增：AABB 结构体定义
struct AABB
{
    glm::vec3 center{0.0f, 0.0f, 0.0f};
    glm::vec3 extents{0.0f, 0.0f, 0.0f}; // 半尺寸 (Half-Size)

    // 默认构造
    AABB() = default;

    // 1：利用极值自动推导
    AABB(const glm::vec3& min, const glm::vec3& max) : center{(max + min) * 0.5f}, extents{max.x - center.x, max.y - center.y, max.z - center.z}
    {

    }

    // 2：手动指定
    AABB(const glm::vec3& inCenter, float iI, float iJ, float iK) : center{ inCenter }, extents{ iI, iJ, iK }
    {

    }

    bool isOnOrForwardPlane(const Plane& plane) const
    {
        const float r = extents.x * std::abs(plane.normal.x) +
                        extents.y * std::abs(plane.normal.y) + 
                        extents.z * std::abs(plane.normal.z);
        return -r <= plane.getSignedDistanceToPlane(center);
    }

    // 🌟 核心判断：结合模型矩阵，动态算出世界空间下的 AABB 并碰撞
    bool isOnFrustum(const Frustum& camFrustum, const glm::mat4& modelMatrix) const
    {
        glm::vec3 globalCenter = glm::vec3(modelMatrix * glm::vec4(center, 1.0f));

        glm::vec3 right   = glm::vec3(modelMatrix[0]) * extents.x;
        glm::vec3 up      = glm::vec3(modelMatrix[1]) * extents.y;
        glm::vec3 forward = glm::vec3(modelMatrix[2]) * extents.z;

        const float newIi = std::abs(right.x) + std::abs(up.x) + std::abs(forward.x);
        const float newIj = std::abs(right.y) + std::abs(up.y) + std::abs(forward.y);
        const float newIk = std::abs(right.z) + std::abs(up.z) + std::abs(forward.z);

        const AABB globalAABB(globalCenter, newIi, newIj, newIk);

        return (globalAABB.isOnOrForwardPlane(camFrustum.leftFace) &&
                globalAABB.isOnOrForwardPlane(camFrustum.rightFace) &&
                globalAABB.isOnOrForwardPlane(camFrustum.topFace) &&
                globalAABB.isOnOrForwardPlane(camFrustum.bottomFace) &&
                globalAABB.isOnOrForwardPlane(camFrustum.nearFace) &&
                globalAABB.isOnOrForwardPlane(camFrustum.farFace));
    }
};

#endif