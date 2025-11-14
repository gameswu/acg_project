#include "BVH.h"
#include <algorithm>
#include <limits>

namespace ACG {

// AABB implementation

AABB::AABB() 
    : min(std::numeric_limits<float>::max())
    , max(std::numeric_limits<float>::lowest())
{
}

AABB::AABB(const glm::vec3& min, const glm::vec3& max)
    : min(min), max(max)
{
}

bool AABB::Intersect(const glm::vec3& origin, const glm::vec3& direction, float& tMin, float& tMax) const {
    // TODO: Implement ray-AABB intersection (slab method)
    return true;
}

void AABB::Expand(const AABB& other) {
    min = glm::min(min, other.min);
    max = glm::max(max, other.max);
}

void AABB::Expand(const glm::vec3& point) {
    min = glm::min(min, point);
    max = glm::max(max, point);
}

glm::vec3 AABB::Center() const {
    return (min + max) * 0.5f;
}

float AABB::SurfaceArea() const {
    glm::vec3 extent = max - min;
    return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}

// BVH implementation

BVH::BVH() 
    : m_maxDepth(0)
{
}

BVH::~BVH() {
}

void BVH::Build(const std::vector<glm::vec3>& vertices, 
                const std::vector<uint32_t>& indices) {
    // TODO: Prepare triangle list
    m_triangles.clear();
    m_nodes.clear();
    
    // Build BVH tree
    // int rootIndex = BuildRecursive(0, m_triangles.size(), 0);
    // or use SAH:
    // int rootIndex = BuildWithSAH(0, m_triangles.size(), 0);
}

bool BVH::Intersect(const glm::vec3& origin, const glm::vec3& direction,
                    float& t, int& triangleIndex) const {
    // TODO: Implement BVH traversal and ray-triangle intersection
    return false;
}

int BVH::BuildRecursive(int start, int end, int depth) {
    // TODO: Implement recursive BVH construction
    // This is a simple median split approach
    return 0;
}

int BVH::BuildWithSAH(int start, int end, int depth) {
    // TODO: Implement BVH construction with Surface Area Heuristic
    // SAH chooses the best split plane to minimize ray intersection cost
    return 0;
}

AABB BVH::ComputeBounds(int start, int end) const {
    // TODO: Compute bounding box for triangles in range [start, end)
    return AABB();
}

AABB BVH::ComputeCentroidBounds(int start, int end) const {
    // TODO: Compute bounding box of triangle centroids
    return AABB();
}

float BVH::EvaluateSAH(int start, int end, int axis, float pos) const {
    // TODO: Evaluate Surface Area Heuristic cost for a split
    return 0.0f;
}

void BVH::Partition(int start, int end, int axis, float pos, int& mid) {
    // TODO: Partition triangles based on split position
    mid = (start + end) / 2;
}

} // namespace ACG
