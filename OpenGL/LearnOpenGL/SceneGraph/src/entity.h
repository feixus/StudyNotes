#ifndef __ENTITY__
#define __ENTITY__

#include "model.h"
#include "camera.h"
#include "shader.h"

#include <glm/glm.hpp>

#include <list>
#include <array>
#include <memory>

class Transform
{
protected:
    //local space infomation
    glm::vec3 m_pos = {0.0f, 0.0f, 0.0f};
    glm::vec3 m_eulerRot = {0.0f, 0.0f, 0.0f};  //in degrees
    glm::vec3 m_scale = {1.0f, 1.0f, 1.0f};

    //global space information concatenate in matrix
    glm::mat4 m_modelMatrix = glm::mat4(1.0f);

    bool m_isDirty = true;

protected:
    glm::mat4 getLocalModelMatrix()
    {
        const glm::mat4 transformX = glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerRot.x), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 transformY = glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerRot.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 transformZ = glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerRot.z), glm::vec3(0.0f, 0.0f, 1.0f));

        const glm::mat4 rotationMatrix = transformY * transformX * transformZ;

        return glm::translate(glm::mat4(1.0f), m_pos) * rotationMatrix * glm::scale(glm::mat4(1.0f), m_scale);
    }

public:
    void computeModelMatrix()
    {
        m_modelMatrix = getLocalModelMatrix();
    }

    void computeModelMatrix(const glm::mat4& parentGlobalModelMatrix)
    {
        m_modelMatrix = parentGlobalModelMatrix * getLocalModelMatrix();
    }

    void setLocalPosition(const glm::vec3& newPosition)
    {
        m_pos = newPosition;
        m_isDirty = true;
    }

    void setLocalRotation(const glm::vec3& newRotation)
    {
        m_eulerRot = newRotation;
        m_isDirty = true;
    }

    void setLocalScale(const glm::vec3& newScale)
    {
        m_scale = newScale;
        m_isDirty = true;
    }

    const glm::vec3& getGlobalPosition() const
    {
        return m_modelMatrix[3];
    }

    const glm::vec3& getLocalPosition() const
    {
        return m_pos;
    }

    const glm::vec3& getLocalRotation() const
    {
        return m_eulerRot;
    }

    const glm::vec3& getLocalScale() const
    {
        return m_scale;
    }

    const glm::mat4& getModelMatrix() const
    {
        return m_modelMatrix;
    }

    glm::vec3 getRight() const
    {
        return m_modelMatrix[0];
    }

    glm::vec3 getUp() const
    {
        return m_modelMatrix[1];
    }

    glm::vec3 getBackward() const
    {
        return m_modelMatrix[2];
    }

    glm::vec3 getForward() const
    {
        return -m_modelMatrix[2];
    }

    glm::vec3 getGlobalScale() const
    {
        return {glm::length(getRight()), glm::length(getUp()), glm::length(getBackward())};
    }

    bool isDirty() const
    {
        return m_isDirty;
    }
};

//六个面的法线朝着棱锥体的内部
struct Plan
{
    glm::vec3 normal = { 0.f, 1.f, 0.f };
    float distance = 0.f;

    Plan() = default;

    Plan(const glm::vec3& pl, const glm::vec3& norm) : normal(glm::normalize(norm)), distance(glm::dot(normal, pl))
    {}

    //点乘的距离 - plan至摄像机的距离???
    float getSignedDistanceToPlan(const glm::vec3& point) const
    {
        return glm::dot(normal, point) - distance;
    }
};

struct Frustum
{
    Plan topFace;
    Plan bottomFace;

    Plan rightFace;
    Plan leftFace;

    Plan farFace;
    Plan nearFace;
};

struct BoundingVolume
{
    virtual bool isOnFrustum(const Frustum& camFrustum, const Transform& transform) const = 0;

    virtual bool isOnOrForwardPlan(const Plan& plan) const = 0;

    bool isOnFrustum(const Frustum& camFrustum) const
    {
        return (isOnOrForwardPlan(camFrustum.leftFace) &&
            isOnOrForwardPlan(camFrustum.rightFace) &&
            isOnOrForwardPlan(camFrustum.topFace) &&
            isOnOrForwardPlan(camFrustum.bottomFace) &&
            isOnOrForwardPlan(camFrustum.nearFace) &&
            isOnOrForwardPlan(camFrustum.farFace));
    };
};

struct Sphere : public BoundingVolume
{
    glm::vec3 center{0.f, 0.f, 0.f};
    float radius{ 0.f };

    Sphere(const glm::vec3& inCenter, float inRadius)
        : BoundingVolume{}, center{ inCenter }, radius { inRadius }
    {}

    bool isOnOrForwardPlan(const Plan& plan) const final
    {
        return plan.getSignedDistanceToPlan(center) > -radius;
    }

    bool isOnFrustum(const Frustum& camFrustum, const Transform& transform) const final
    {
        const glm::vec3 globalScale = transform.getGlobalScale();
        const glm::vec3 globalCenter{ transform.getModelMatrix() * glm::vec4(center, 1.f) };
        const float maxScale = std::max(std::max(globalScale.x, globalScale.y), globalScale.z);

        Sphere globalSphere(globalCenter, radius * (maxScale * 0.5f));

        return (globalSphere.isOnOrForwardPlan(camFrustum.leftFace) &&
            globalSphere.isOnOrForwardPlan(camFrustum.rightFace) &&
            globalSphere.isOnOrForwardPlan(camFrustum.farFace) &&
            globalSphere.isOnOrForwardPlan(camFrustum.nearFace) &&
            globalSphere.isOnOrForwardPlan(camFrustum.topFace) &&
            globalSphere.isOnOrForwardPlan(camFrustum.bottomFace));
    };
};

struct SquareAABB : public BoundingVolume
{
    glm::vec3 center{ 0.f, 0.f, 0.f };
    float extent { 0.f };

    SquareAABB(const glm::vec3& inCenter, float inExtent)
        : BoundingVolume{}, center {inCenter}, extent{inExtent}
    {}

    bool isOnOrForwardPlan(const Plan& plan) const final
    {
        const float r = extent * (std::abs(plan.normal.x) + std::abs(plan.normal.y) + std::abs(plan.normal.z));
        return -r <= plan.getSignedDistanceToPlan(center);
    }

    bool isOnFrustum(const Frustum& camFrustum, const Transform& transform) const final
    {
        const glm::vec3 globalCenter {transform.getModelMatrix() * glm::vec4(center, 1.f)};

        const glm::vec3 right = transform.getRight() * extent;
        const glm::vec3 up = transform.getUp() * extent;
        const glm::vec3 forward = transform.getForward() * extent;

        const float newIi = std::abs(glm::dot(glm::vec3{1.f, 0.f, 0.f}, right)) +
            std::abs(glm::dot(glm::vec3{1.f, 0.f, 0.f}, up)) +
            std::abs(glm::dot(glm::vec3{1.0f, 0.f, 0.f}, forward));

        const float newIj = std::abs(glm::dot(glm::vec3{0.f, 1.f, 0.f}, right)) +
            std::abs(glm::dot(glm::vec3{0.f, 1.f, 0.f}, up)) +
            std::abs(glm::dot(glm::vec3{0.0f, 1.f, 0.f}, forward));

        const float newIk = std::abs(glm::dot(glm::vec3{0.f, 0.f, 1.f}, right)) +
        std::abs(glm::dot(glm::vec3{0.f, 0.f, 1.f}, up)) +
        std::abs(glm::dot(glm::vec3{0.0f, 0.f, 1.f}, forward));

        const SquareAABB globalAABB(globalCenter, std::max(std::max(newIi, newIj), newIk));

        return (globalAABB.isOnOrForwardPlan(camFrustum.leftFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.rightFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.farFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.nearFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.topFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.bottomFace));
    };
};

struct AABB : public BoundingVolume
{
    glm::vec3 center{0.f, 0.f, 0.f};
    glm::vec3 extents{0.f, 0.f, 0.f};

    AABB(const glm::vec3& min, const glm::vec3& max)
        : BoundingVolume{}, center{(max + min) * 0.5f}, extents{max.x - center.x, max.y - center.y, max.z - center.z}
    {}

    AABB(const glm::vec3& inCenter, float iI, float iJ, float iK)
        : BoundingVolume{}, center{inCenter}, extents{iI, iJ, iK}
    {}

    std::array<glm::vec3, 8> getVertice() const
    {
        std::array<glm::vec3, 8> vertice;
        vertice[0] = {center.x - extents.x, center.y - extents.y, center.z - extents.z};
        vertice[1] = {center.x + extents.x, center.y - extents.y, center.z - extents.z};
        vertice[2] = {center.x - extents.x, center.y + extents.y, center.z - extents.z};
        vertice[3] = {center.x + extents.x, center.y + extents.y, center.z - extents.z};
        vertice[4] = {center.x - extents.x, center.y - extents.y, center.z + extents.z};
        vertice[5] = {center.x + extents.x, center.y - extents.y, center.z + extents.z};
        vertice[6] = {center.x - extents.x, center.y + extents.y, center.z + extents.z};
        vertice[7] = {center.x + extents.x, center.y + extents.y, center.z + extents.z};
        return vertice;
    }

    //see https://gdbooks.gitbooks.io/3dcollisions/content/Chapter2/static_aabb_plan.html
    bool isOnOrForwardPlan(const Plan& plan) const final
    {
        const float r = extents.x * std::abs(plan.normal.x) + extents.y * std::abs(plan.normal.y) +
            extents.z * std::abs(plan.normal.z);
        
        return -r <= plan.getSignedDistanceToPlan(center);
    }

    bool isOnFrustum(const Frustum& camFrustum, const Transform& transform) const final
    {
        const glm::vec3 globalCenter{ transform.getModelMatrix() * glm::vec4(center, 1.f) };

        const glm::vec3 right = transform.getRight() * extents.x;
        const glm::vec3 up = transform.getUp() * extents.y;
        const glm::vec3 forward = transform.getForward() * extents.z;

        const float newIi = std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f}, up)) +
            std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f}, forward));
        
        const float newIj = std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f}, up)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f}, forward));

        const float newIk = std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f}, up)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f}, forward));

        const AABB globalAABB(globalCenter, newIi, newIj, newIk);

        return (globalAABB.isOnOrForwardPlan(camFrustum.leftFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.rightFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.farFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.nearFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.topFace) &&
            globalAABB.isOnOrForwardPlan(camFrustum.bottomFace));
    }
};

Frustum createFrustumFromCamera(const Camera& cam, float aspect, float fovY, float zNear, float zFar);
AABB generateAABB(const Model& model);
Sphere generateSphereBV(const Model& model);

class Entity
{
public:
    //scene graph
    std::list<std::unique_ptr<Entity>> children;
    Entity* parent = nullptr;

    Transform transform;

    Model* pModel = nullptr;
    std::unique_ptr<AABB> boundingVolume;

    Entity(Model& model) : pModel{ &model }
    {
        boundingVolume = std::make_unique<AABB>(generateAABB(model));
        // boundingVolume = std::make_unique<AABB>(generateSphereBV(model));
    }

    Entity() {}


    AABB getGlobalAABB()
    {
        const glm::vec3 globalCenter{ transform.getModelMatrix() * glm::vec4(boundingVolume->center, 1.f) };

        const glm::vec3 right = transform.getRight() * boundingVolume->extents.x;
        const glm::vec3 up = transform.getUp() * boundingVolume->extents.y;
        const glm::vec3 forward = transform.getForward() * boundingVolume->extents.z;

        const float newIi = std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, up)) +
            std::abs(glm::dot(glm::vec3{ 1.f, 0.f, 0.f }, forward));

        const float newIj = std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, up)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 1.f, 0.f }, forward));

        const float newIk = std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, right)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, up)) +
            std::abs(glm::dot(glm::vec3{ 0.f, 0.f, 1.f }, forward));

        return AABB(globalCenter, newIi, newIj, newIk);
    }

    template<typename... TArgs>
    void addChild(TArgs&... args)
    {
        children.emplace_back(std::make_unique<Entity>(args...));
        children.back()->parent = this;
    }

    void updateSelfAndChild()
    {
        if (!transform.isDirty())
            return;
        
        forceUpdateSelfAndChild();
    }

    void forceUpdateSelfAndChild()
    {
        if (parent)
            transform.computeModelMatrix(parent->transform.getModelMatrix());
        else
            transform.computeModelMatrix();
        
        for (auto&& child : children)
        {
            child->forceUpdateSelfAndChild();
        }
    }

    void drawSelfAndChild(const Frustum& frustum, Shader& ourShader, unsigned int& display, unsigned int& total)
    {
        if (boundingVolume->isOnFrustum(frustum, transform))
        {
            ourShader.setMat4("model", transform.getModelMatrix());
            pModel->Draw(ourShader);
            display++;
        }
        total++;

        for (auto&& child : children)
        {
            child->drawSelfAndChild(frustum, ourShader, display, total);
        }
    }
};

#endif