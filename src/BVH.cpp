#include "BVH.h"
#include "MathUtils.h"
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
    // Slab method for ray-AABB intersection
    glm::vec3 invDir = 1.0f / direction;
    glm::vec3 t0 = (min - origin) * invDir;
    glm::vec3 t1 = (max - origin) * invDir;
    
    glm::vec3 tSmaller = glm::min(t0, t1);
    glm::vec3 tBigger = glm::max(t0, t1);
    
    tMin = std::max(std::max(tSmaller.x, tSmaller.y), tSmaller.z);
    tMax = std::min(std::min(tBigger.x, tBigger.y), tBigger.z);
    
    return tMin <= tMax && tMax >= 0.0f;
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
    m_triangles.clear();
    m_nodes.clear();
    
    // Prepare triangle list
    for (size_t i = 0; i < indices.size(); i += 3) {
        Triangle tri;
        tri.v0 = vertices[indices[i]];
        tri.v1 = vertices[indices[i + 1]];
        tri.v2 = vertices[indices[i + 2]];
        tri.centroid = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        tri.index = static_cast<int>(i / 3);
        m_triangles.push_back(tri);
    }
    
    if (m_triangles.empty()) {
        return;
    }
    
    // Build BVH tree using SAH
    m_maxDepth = 0;
    BuildWithSAH(0, static_cast<int>(m_triangles.size()), 0);
}

bool BVH::Intersect(const glm::vec3& origin, const glm::vec3& direction,
                    float& t, int& triangleIndex) const {
    if (m_nodes.empty()) {
        return false;
    }
    
    t = std::numeric_limits<float>::max();
    triangleIndex = -1;
    bool hit = false;
    
    // Stack-based traversal
    int stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;
    
    while (stackPtr > 0) {
        int nodeIdx = stack[--stackPtr];
        const BVHNode& node = m_nodes[nodeIdx];
        
        float tMin, tMax;
        if (!node.bbox.Intersect(origin, direction, tMin, tMax) || tMin > t) {
            continue;
        }
        
        if (node.primCount > 0) {
            // Leaf node - test triangles
            for (int i = 0; i < node.primCount; ++i) {
                const Triangle& tri = m_triangles[node.firstPrim + i];
                float u, v, triT;
                if (MathUtils::RayTriangleIntersect(
                    Ray(origin, direction), tri.v0, tri.v1, tri.v2, triT, u, v)) {
                    if (triT < t) {
                        t = triT;
                        triangleIndex = tri.index;
                        hit = true;
                    }
                }
            }
        } else {
            // Interior node - traverse children
            if (node.leftChild >= 0) stack[stackPtr++] = node.leftChild;
            if (node.rightChild >= 0) stack[stackPtr++] = node.rightChild;
        }
    }
    
    return hit;
}

int BVH::BuildRecursive(int start, int end, int depth) {
    m_maxDepth = std::max(m_maxDepth, depth);
    
    BVHNode node;
    node.bbox = ComputeBounds(start, end);
    node.firstPrim = start;
    node.primCount = end - start;
    node.leftChild = -1;
    node.rightChild = -1;
    
    int nodeIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back(node);
    
    // Leaf node condition
    if (node.primCount <= 4 || depth >= 32) {
        return nodeIdx;
    }
    
    // Find longest axis
    AABB centroidBounds = ComputeCentroidBounds(start, end);
    glm::vec3 extent = centroidBounds.max - centroidBounds.min;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    
    // Split at median
    int mid = (start + end) / 2;
    std::nth_element(m_triangles.begin() + start, m_triangles.begin() + mid,
                     m_triangles.begin() + end,
                     [axis](const Triangle& a, const Triangle& b) {
                         return a.centroid[axis] < b.centroid[axis];
                     });
    
    // Build children
    m_nodes[nodeIdx].primCount = 0;
    m_nodes[nodeIdx].leftChild = BuildRecursive(start, mid, depth + 1);
    m_nodes[nodeIdx].rightChild = BuildRecursive(mid, end, depth + 1);
    
    return nodeIdx;
}

int BVH::BuildWithSAH(int start, int end, int depth) {
    m_maxDepth = std::max(m_maxDepth, depth);
    
    BVHNode node;
    node.bbox = ComputeBounds(start, end);
    node.firstPrim = start;
    node.primCount = end - start;
    node.leftChild = -1;
    node.rightChild = -1;
    
    int nodeIdx = static_cast<int>(m_nodes.size());
    m_nodes.push_back(node);
    
    // Leaf node condition
    if (node.primCount <= 4 || depth >= 32) {
        return nodeIdx;
    }
    
    // Find best split using SAH
    AABB centroidBounds = ComputeCentroidBounds(start, end);
    glm::vec3 extent = centroidBounds.max - centroidBounds.min;
    
    int bestAxis = 0;
    float bestPos = 0.0f;
    float bestCost = std::numeric_limits<float>::max();
    
    // Try each axis
    for (int axis = 0; axis < 3; ++axis) {
        if (extent[axis] < 1e-6f) continue;
        
        // Try several split positions
        const int numSplits = 8;
        for (int i = 1; i < numSplits; ++i) {
            float t = static_cast<float>(i) / numSplits;
            float pos = centroidBounds.min[axis] + t * extent[axis];
            float cost = EvaluateSAH(start, end, axis, pos);
            
            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestPos = pos;
            }
        }
    }
    
    // Partition triangles
    int mid;
    Partition(start, end, bestAxis, bestPos, mid);
    
    // Avoid degenerate splits
    if (mid == start || mid == end) {
        return nodeIdx;
    }
    
    // Build children
    m_nodes[nodeIdx].primCount = 0;
    m_nodes[nodeIdx].leftChild = BuildWithSAH(start, mid, depth + 1);
    m_nodes[nodeIdx].rightChild = BuildWithSAH(mid, end, depth + 1);
    
    return nodeIdx;
}

AABB BVH::ComputeBounds(int start, int end) const {
    AABB bounds;
    for (int i = start; i < end; ++i) {
        bounds.Expand(m_triangles[i].v0);
        bounds.Expand(m_triangles[i].v1);
        bounds.Expand(m_triangles[i].v2);
    }
    return bounds;
}

AABB BVH::ComputeCentroidBounds(int start, int end) const {
    AABB bounds;
    for (int i = start; i < end; ++i) {
        bounds.Expand(m_triangles[i].centroid);
    }
    return bounds;
}

float BVH::EvaluateSAH(int start, int end, int axis, float pos) const {
    AABB leftBox, rightBox;
    int leftCount = 0, rightCount = 0;
    
    for (int i = start; i < end; ++i) {
        if (m_triangles[i].centroid[axis] < pos) {
            leftBox.Expand(m_triangles[i].v0);
            leftBox.Expand(m_triangles[i].v1);
            leftBox.Expand(m_triangles[i].v2);
            leftCount++;
        } else {
            rightBox.Expand(m_triangles[i].v0);
            rightBox.Expand(m_triangles[i].v1);
            rightBox.Expand(m_triangles[i].v2);
            rightCount++;
        }
    }
    
    // Avoid division by zero
    if (leftCount == 0 || rightCount == 0) {
        return std::numeric_limits<float>::max();
    }
    
    // SAH cost = leftArea * leftCount + rightArea * rightCount
    float cost = leftBox.SurfaceArea() * leftCount + rightBox.SurfaceArea() * rightCount;
    return cost;
}

void BVH::Partition(int start, int end, int axis, float pos, int& mid) {
    mid = start;
    for (int i = start; i < end; ++i) {
        if (m_triangles[i].centroid[axis] < pos) {
            std::swap(m_triangles[i], m_triangles[mid]);
            mid++;
        }
    }
    
    // Ensure mid is valid
    if (mid == start) mid = start + 1;
    if (mid == end) mid = end - 1;
}

} // namespace ACG
