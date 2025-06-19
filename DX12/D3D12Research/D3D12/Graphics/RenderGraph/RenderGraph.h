#pragma once
#include "Graphics/GraphicsTexture.h"
#include "Graphics/GraphicsBuffer.h"
#include "RenderGraphDefinition.h"

class Graphics;
class CommandContext;
class RGResourceAllocator;
class RGGraph;
class RGPassBase;

enum class RGResourceType
{
    None,
    Texture,
    Buffer,
};

class RGResource
{
public:
    RGResource(const char* pName, int id, bool isImported, RGResourceType type, void* pResource)
        : m_Name(pName), m_Id(id), m_IsImported(isImported), m_Type(type), m_pPhysicalResource(pResource) {}

    const char* m_Name;
    int m_Id;
    bool m_IsImported;
    int m_Version;
    RGResourceType m_Type;
    void* m_pPhysicalResource;

    // render graph compile-time values
    int m_References{0};
};

class RGTexture : public RGResource
{
public:
    RGTexture(const char* pName, int id, const TextureDesc& desc, GraphicsTexture* pTexture)
        : RGResource(pName, id, pTexture != nullptr, RGResourceType::Texture, pTexture), m_Desc(desc)  
    {}

    const TextureDesc& GetDesc() const { return m_Desc; }

private:
    TextureDesc m_Desc;
};

class RGBuffer : public RGResource
{
public:
    RGBuffer(const char* pName, int id, const BufferDesc& desc, Buffer* pBuffer)
        : RGResource(pName, id, pBuffer != nullptr, RGResourceType::Buffer, pBuffer), m_Desc(desc) 
    {}

    const BufferDesc& GetDesc() const { return m_Desc; }

private:
    BufferDesc m_Desc{};
};

struct RGNode
{
public:
    RGNode(RGResource* pResource) : m_pResource(pResource), m_Version(pResource->m_Version) {}

    RGResource* m_pResource;
    int m_Version;

    // render graph compile-time values
    RGPassBase* m_pWriter{nullptr};
    int m_Reads{0};
};

struct RGResourceHandle
{
    RGResourceHandle() = default;
    explicit RGResourceHandle(int id) : Index(id) {}

    bool operator==(const RGResourceHandle& other) const { return Index == other.Index;}
    bool operator!=(const RGResourceHandle& other) const { return Index != other.Index;}

    constexpr static const int InvalidIndex{-1};

    inline void Invalidate() { Index = InvalidIndex; }
    inline bool IsValid() const { return Index != InvalidIndex; }

    int Index{InvalidIndex};
};

struct RGResourceHandleMutable : public RGResourceHandle
{
    RGResourceHandleMutable() = default;
    explicit RGResourceHandleMutable(int id) : RGResourceHandle(id) {}
};

class RGPassBuilder
{
public:
    RGPassBuilder(RGGraph& renderGraph, RGPassBase& pass)
        : m_RenderGraph(renderGraph), m_Pass(pass) {}

    RGPassBuilder(const RGPassBuilder& other) = delete;
    RGPassBuilder& operator=(const RGPassBuilder& other) = delete;

    RGResourceHandle Read(const RGResourceHandle& resource);
    RGResourceHandleMutable& Write(RGResourceHandleMutable& resource);
    RGResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc);
    RGResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc);

    void NeverCull();

private:
    RGPassBase& m_Pass;
    RGGraph& m_RenderGraph;
};

class RGPassResource
{
public:
    RGPassResource(RGGraph& graph, RGPassBase& pass)
        : m_Graph(graph), m_Pass(pass) {}

    RGPassResource(const RGPassResource& other) = delete;
    RGPassResource& operator=(const RGPassResource& other) = delete;

    template<typename T>
    T* GetResource(RGResourceHandle handle) const
    {
        return static_cast<T*>(GetResourceInternal(handle)->m_pPhysicalResource);
    }

private:
    RGResource* GetResourceInternal(RGResourceHandle handle) const;

    RGGraph& m_Graph;
    RGPassBase& m_Pass;
};

class RGPassBase
{
public:
    friend class RGPassBuilder;
    friend class RGGraph;

    RGPassBase(RGGraph& graph, const char* pName, int id)
        : m_Name(pName), m_RenderGraph(graph), m_Id(id) {}

    virtual ~RGPassBase() = default;

    virtual void Execute(const RGPassResource& resources, CommandContext& renderContext) = 0;

    const char* GetName() const { return m_Name; }

    bool ReadsFrom(RGResourceHandle handle) const
    {
        return std::find(m_Reads.begin(), m_Reads.end(), handle) != m_Reads.end();
    }

    bool WritesTo(RGResourceHandle handle) const
    {
        return std::find(m_Writes.begin(), m_Writes.end(), handle) != m_Writes.end();
    }
    
protected:
    const char* m_Name;
    std::vector<RGResourceHandle> m_Reads;
    std::vector<RGResourceHandle> m_Writes;
    RGGraph& m_RenderGraph;
    int m_Id;
    bool m_NeverCull{false};

    // render graph compile-time values
    int m_References{0};
};

template<typename PassData>
class RGPass : public RGPassBase
{
    friend class RGGraph;

public:
    RGPass(RGGraph& graph, const char* pName, int id)
        : RGPassBase(graph, pName, id), m_PassData{} {}

    const PassData& GetData() const { return m_PassData; }
    PassData& GetData() { return m_PassData; }

protected:
    PassData m_PassData;
};

template<typename PassData, typename ExecuteCallback>
class LambdaRenderPass : public RGPass<PassData>
{
public:
    LambdaRenderPass(RGGraph& graph, const char* pName, int id, ExecuteCallback&& executeCallback)
        : RGPass<PassData>(graph, pName, id), m_ExecuteCallback(std::forward<ExecuteCallback>(executeCallback)) {}

    virtual void Execute(const RGPassResource& resources, CommandContext& renderContext) override
    {
        m_ExecuteCallback(renderContext, resources, this->m_PassData);
    }

private:
    ExecuteCallback m_ExecuteCallback;
};

class RGGraph
{
public:
    RGGraph(RGResourceAllocator* pAllocator);
    ~RGGraph();

    RGGraph(const RGGraph& other) = delete;
    RGGraph& operator=(const RGGraph& other) = delete;

    void Compile();
    int64_t Execute(Graphics* pGraphics);
    void Present(RGResourceHandle resource);

    void DumpGraphViz(const char* pPath) const;
    void DumpGraphMermaid(const char* pPath) const;

    RGResourceHandle MoveResource(RGResourceHandle from, RGResourceHandle to);

    template<typename PassData, typename SetupCallback, typename ExecuteCallback>
    RGPass<PassData>& AddCallbackPass(const char* pName, const SetupCallback& setupCallback, ExecuteCallback&& executeCallback)
    {
        RG_STATIC_ASSERT(sizeof(ExecuteCallback) < 1024, "The Execute callback exceeds the maximum size");
        RGPass<PassData>* pPass = new LambdaRenderPass<PassData, ExecuteCallback>(*this, pName, (int)m_RenderPasses.size(), std::forward<ExecuteCallback>(executeCallback));
        RGPassBuilder builder(*this, *pPass);
        setupCallback(builder, pPass->GetData());
        m_RenderPasses.push_back(pPass);
        return *pPass;
    }

    RGResourceHandleMutable CreateTexture(const char* pName, const TextureDesc& desc)
    {
        RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), desc, nullptr);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandleMutable ImportTexture(const char* pName, GraphicsTexture* pTexture)
    {
        assert(pTexture);
        RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandleMutable CreateBuffer(const char* pName, const BufferDesc& desc)
    {
        RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, nullptr);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandleMutable ImportBuffer(const char* pName, Buffer* pBuffer)
    {
        assert(pBuffer);
        BufferDesc desc{};
        RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, pBuffer);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandleMutable CreateResourceNode(RGResource* pResource)
    {
        RGNode node(pResource);
        m_RGNodes.push_back(node);
        return RGResourceHandleMutable((int)m_RGNodes.size() - 1);
    }

    bool IsValidHandle(RGResourceHandle handle) const
    {
        return handle.IsValid() && handle.Index < (int)m_RGNodes.size();
    }

    const RGNode& GetResourceNode(RGResourceHandle handle) const
    {
        RG_ASSERT(IsValidHandle(handle), "Invalid handle");
        return m_RGNodes[handle.Index];
    }

    RGResource* GetResource(RGResourceHandle handle) const
    {
        const RGNode& node = GetResourceNode(handle);
        return node.m_pResource;
    }

private:
    void ExecutePass(RGPassBase* pPass, CommandContext& renderContext, RGResourceAllocator* pAllocator);
    void PrepareResources(RGPassBase* pPass, RGResourceAllocator* pAllocator);
    void ReleaseResources(RGPassBase* pPass, RGResourceAllocator* pAllocator);
    void DestroyData();

    void ConditionallyCreateResource(RGResource* pResource, RGResourceAllocator* pAllocator);
    void ConditionallyDestroyResource(RGResource* pResource, RGResourceAllocator* pAllocator);

    struct ResourceAlias
    {
        RGResourceHandle From;
        RGResourceHandle To;
    };

    RGResourceAllocator* m_pAllocator;
    std::vector<ResourceAlias> m_Aliases;
    std::vector<RGPassBase*> m_RenderPasses;
    std::vector<RGResource*> m_Resources;
    std::vector<RGNode> m_RGNodes;
};

