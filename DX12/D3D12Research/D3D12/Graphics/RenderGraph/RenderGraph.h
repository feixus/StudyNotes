#pragma once
#include "Graphics/Core/GraphicsTexture.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "RenderGraphDefinition.h"

class Graphics;
class CommandContext;
class RGResourceAllocator;
class RGGraph;
class RGPass;

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
        : m_Name(pName), m_Id(id), m_IsImported(isImported), m_Version(0), m_Type(type), m_pPhysicalResource(pResource), m_References(0) {}

    const char* m_Name;
    int m_Id;
    bool m_IsImported;
    int m_Version;
    RGResourceType m_Type;
    void* m_pPhysicalResource;

    // render graph compile-time values
    int m_References;
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
    RGPass* m_pWriter{nullptr};
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

class RGPassBuilder
{
public:
    RGPassBuilder(RGGraph& renderGraph, RGPass& pass)
        : m_RenderGraph(renderGraph), m_Pass(pass) {}

    RGPassBuilder(const RGPassBuilder& other) = delete;
    RGPassBuilder& operator=(const RGPassBuilder& other) = delete;

    template<typename ExecuteCallback>
    void Bind(ExecuteCallback&& callback);

    RGResourceHandle Read(const RGResourceHandle& resource);
    [[nodiscard]]RGResourceHandle& Write(RGResourceHandle& resource);
    RGResourceHandle CreateTexture(const char* pName, const TextureDesc& desc);
    RGResourceHandle CreateBuffer(const char* pName, const BufferDesc& desc);

    void NeverCull();

private:
    RGPass& m_Pass;
    RGGraph& m_RenderGraph;
};

class RGPassResource
{
public:
    RGPassResource(RGGraph& graph, RGPass& pass)
        : m_Graph(graph), m_Pass(pass) {}

    RGPassResource(const RGPassResource& other) = delete;
    RGPassResource& operator=(const RGPassResource& other) = delete;

    GraphicsTexture* GetTexture(RGResourceHandle handle) const;

private:
    RGResource* GetResourceInternal(RGResourceHandle handle) const;

    RGGraph& m_Graph;
    RGPass& m_Pass;
};

class IPassExecutor
{
public:
    virtual ~IPassExecutor() = default;
    virtual void Execute(const RGPassResource& resources, CommandContext& renderContext) = 0;
};

template<typename ExecuteCallback>
class PassExecutor : public IPassExecutor
{
public:
    PassExecutor(ExecuteCallback&& callback)
        : m_Callback(std::move(callback))
    {}

    virtual void Execute(const RGPassResource& resources, CommandContext& renderContext) override
    {
        m_Callback(renderContext, resources);
    }

private:
    ExecuteCallback m_Callback;
};

class RGPass
{
public:
    friend class RGPassBuilder;
    friend class RGGraph;

    RGPass(RGGraph& graph, const char* pName, int id)
        : m_Name(pName), m_RenderGraph(graph), m_Id(id) {}

    void Execute(const RGPassResource& resources, CommandContext& renderContext)
    {
        check(m_pPassExecutor);
        m_pPassExecutor->Execute(resources, renderContext);
    }

    template<typename ExecuteCallback>
    void SetCallback(ExecuteCallback&& callback)
    {
        RG_STATIC_ASSERT(sizeof(ExecuteCallback) < 1024, "the execute callback exceeds the maximum size");
        m_pPassExecutor = std::make_unique<PassExecutor<ExecuteCallback>>(std::move(callback));
    }

    const char* GetName() const { return m_Name; }

    bool ReadsFrom(RGResourceHandle handle) const
    {
        return std::find(m_Reads.begin(), m_Reads.end(), handle) != m_Reads.end();
    }

    bool WritesTo(RGResourceHandle handle) const
    {
        return std::find(m_Writes.begin(), m_Writes.end(), handle) != m_Writes.end();
    }
    
private:
    std::unique_ptr<IPassExecutor> m_pPassExecutor;
    const char* m_Name;
    std::vector<RGResourceHandle> m_Reads;
    std::vector<RGResourceHandle> m_Writes;
    RGGraph& m_RenderGraph;
    int m_Id;
    bool m_NeverCull{false};

    // render graph compile-time values
    int m_References{0};
};

class RGGraph
{
public:
    RGGraph(Graphics* pAllocator);
    ~RGGraph();

    RGGraph(const RGGraph& other) = delete;
    RGGraph& operator=(const RGGraph& other) = delete;

    void Compile();
    int64_t Execute();
    void Present(RGResourceHandle resource);

    void DumpGraphViz(const char* pPath) const;
    void DumpGraphMermaid(const char* pPath) const;
  
    RGResourceHandle MoveResource(RGResourceHandle from, RGResourceHandle to);

    RGPassBuilder AddPass(const char* pName)
    {
        RGPass* pPass = new RGPass(*this, pName, (int)m_RenderPasses.size());
        AddPass(pPass);
        return RGPassBuilder(*this, *pPass);
    }

    RGResourceHandle CreateTexture(const char* pName, const TextureDesc& desc)
    {
        RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), desc, nullptr);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandle ImportTexture(const char* pName, GraphicsTexture* pTexture)
    {
        check(pTexture);
        RGResource* pResource = new RGTexture(pName, (int)m_Resources.size(), pTexture->GetDesc(), pTexture);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandle CreateBuffer(const char* pName, const BufferDesc& desc)
    {
        RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, nullptr);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandle ImportBuffer(const char* pName, Buffer* pBuffer)
    {
        check(pBuffer);
        BufferDesc desc{};
        RGResource* pResource = new RGBuffer(pName, (int)m_Resources.size(), desc, pBuffer);
        m_Resources.push_back(pResource);
        return CreateResourceNode(pResource);
    }

    RGResourceHandle CreateResourceNode(RGResource* pResource)
    {
        RGNode node(pResource);
        m_RGNodes.push_back(node);
        return RGResourceHandle((int)m_RGNodes.size() - 1);
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
    RGPass& AddPass(RGPass* pPass);
    void ExecutePass(RGPass* pPass, CommandContext& renderContext);
    void PrepareResources(RGPass* pPass);
    void ReleaseResources(RGPass* pPass);
    void DestroyData();

    void ConditionallyCreateResource(RGResource* pResource);
    void ConditionallyReleaseResource(RGResource* pResource);

    struct ResourceAlias
    {
        RGResourceHandle From;
        RGResourceHandle To;
    };

    Graphics* m_pGraphics;
    std::unique_ptr<RGResourceAllocator> m_pAllocator;
    uint64_t m_LastFenceValue{0};
    bool m_ImmediateMode{false};

    std::vector<ResourceAlias> m_Aliases;
    std::vector<RGPass*> m_RenderPasses;
    std::vector<RGResource*> m_Resources;
    std::vector<RGNode> m_RGNodes;
};

template<typename ExecuteCallback>
void RGPassBuilder::Bind(ExecuteCallback&& callback)
{
	m_Pass.SetCallback(std::move(callback));
}