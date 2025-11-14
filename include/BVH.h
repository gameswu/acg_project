#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace ACG {

/**
 * @brief Axis-Aligned Bounding Box
 */
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    AABB();
    AABB(const glm::vec3& min, const glm::vec3& max);
    
    bool Intersect(const glm::vec3& origin, const glm::vec3& direction, float& tMin, float& tMax) const;
    void Expand(const AABB& other);
    void Expand(const glm::vec3& point);
    glm::vec3 Center() const;
    float SurfaceArea() const;
};

/**
 * @brief BVH Node
 */
struct BVHNode {
    AABB bbox;
    int leftChild;   // Index of left child, -1 if leaf
    int rightChild;  // Index of right child, -1 if leaf
    int firstPrim;   // Index of first primitive (for leaf nodes)
    int primCount;   // Number of primitives (for leaf nodes)
};

/**
 * @brief Bounding Volume Hierarchy for ray-scene intersection acceleration
 * 
 * Implements BVH construction with Surface Area Heuristic (SAH)
 */
class BVH {
public:
    BVH();
    ~BVH();

    // Build BVH from triangles
    void Build(const std::vector<glm::vec3>& vertices, 
               const std::vector<uint32_t>& indices);
    
    // Ray intersection
    bool Intersect(const glm::vec3& origin, const glm::vec3& direction,
                   float& t, int& triangleIndex) const;
    
    // Get BVH data for GPU
    const std::vector<BVHNode>& GetNodes() const { return m_nodes; }
    
    // Statistics
    int GetNodeCount() const { return static_cast<int>(m_nodes.size()); }
    int GetMaxDepth() const { return m_maxDepth; }

private:
    struct Triangle {
        glm::vec3 v0, v1, v2;
        glm::vec3 centroid;
        int index;
    };
    
    std::vector<BVHNode> m_nodes;
    std::vector<Triangle> m_triangles;
    int m_maxDepth;
    
    // Build methods
    int BuildRecursive(int start, int end, int depth);
    int BuildWithSAH(int start, int end, int depth);
    
    // Helper methods
    AABB ComputeBounds(int start, int end) const;
    AABB ComputeCentroidBounds(int start, int end) const;
    float EvaluateSAH(int start, int end, int axis, float pos) const;
    void Partition(int start, int end, int axis, float pos, int& mid);
};

} // namespace ACG
