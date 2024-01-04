#include "Renderer.h"

using namespace glm;

// Helper functions

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
        throw std::exception();
}

// Renderer

Renderer::Renderer(xwin::Window& window)
{
    m_Window;

    // Initialization
    m_Factory = nullptr;
    m_Adapter = nullptr;
#if defined(_DEBUG)
    m_DebugController = nullptr;
#endif
    m_Device = nullptr;
    m_CommandQueue = nullptr;
    m_CommandAllocator = nullptr;
    m_CommandList = nullptr;
    m_Swapchain = nullptr;

    // Resources

    m_VertexBuffer = nullptr;
    m_IndexBuffer = nullptr;

	m_UniformBuffer = nullptr;
	m_UniformBufferHeap = nullptr;
	m_MappedUniformBuffer = nullptr;

    m_RootSignature = nullptr;
    m_PipelineState = nullptr;

    // Current Frame
    m_RtvHeap = nullptr;
    for (size_t i = 0; i < s_BackbufferCount; ++i)
        m_RenderTargets[i] = nullptr;
    
    // Sync
    m_Fence = nullptr;

    InitializeAPI(window);
    InitializeResources();
    SetupCommands();
    m_StartTime = std::chrono::high_resolution_clock::now();
}

Renderer::~Renderer()
{
    if (m_Swapchain != nullptr)
    {
        m_Swapchain->SetFullscreenState(false, nullptr);
        m_Swapchain->Release();
        m_Swapchain = nullptr;
    }

    DestroyCommands();
    DestroyFrameBuffer();
    DestroyResources();
    DestroyAPI();
}

void Renderer::InitializeAPI(xwin::Window& window)
{
    // The renderer needs the window when resizing the swapchain
    m_Window = &window;

    // Create Factory

    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    ID3D12Debug* debugController;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    ThrowIfFailed(debugController->QueryInterface(IID_PPV_ARGS(&m_DebugController)));
    m_DebugController->EnableDebugLayer();
    m_DebugController->SetEnableGPUBasedValidation(true);

    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

    debugController->Release();
    debugController = nullptr;

#endif
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory)));

    // Create Adapter
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_Factory->EnumAdapters1(adapterIndex, &m_Adapter); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        m_Adapter->GetDesc1(&desc);

        // Don't select the Basic Render Driver adapter.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        // Check to see if the adapter supports Direct3D 12, but don't create
        // the actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(m_Adapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            break;

        // We won't use this adapter, so release it
        m_Adapter->Release();
    }

    // Create Device
    ID3D12Device* pDev = nullptr;
    ThrowIfFailed(D3D12CreateDevice(m_Adapter, D3D_FEATURE_LEVEL_12_0,
                                    IID_PPV_ARGS(&m_Device)));

    m_Device->SetName(L"Hello Triangle Device");

#if defined(_DEBUG)
    // Get debug device
    ThrowIfFailed(m_Device->QueryInterface(&m_DebugDevice));
#endif

    // Create Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(
        m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

    // Create Command Allocator
    ThrowIfFailed(m_Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocator)));

    // Sync
    ThrowIfFailed(
        m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));

    // Create Swapchain
    const xwin::WindowDesc wdesc = window.getDesc();
    Resize(wdesc.width, wdesc.height);
}

void Renderer::DestroyAPI()
{
    if (m_Fence)
    {
        m_Fence->Release();
        m_Fence = nullptr;
    }

    if (m_CommandAllocator)
    {
        ThrowIfFailed(m_CommandAllocator->Reset());
        m_CommandAllocator->Release();
        m_CommandAllocator = nullptr;
    }

    if (m_CommandQueue)
    {
        m_CommandQueue->Release();
        m_CommandQueue = nullptr;
    }

    if (m_Device)
    {
        m_Device->Release();
        m_Device = nullptr;
    }

    if (m_Adapter)
    {
        m_Adapter->Release();
        m_Adapter = nullptr;
    }

    if (m_Factory)
    {
        m_Factory->Release();
        m_Factory = nullptr;
    }

#if defined(_DEBUG)
    if (m_DebugController)
    {
        m_DebugController->Release();
        m_DebugController = nullptr;
    }

    D3D12_RLDO_FLAGS flags =
        D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL;

    m_DebugDevice->ReportLiveDeviceObjects(flags);

    if (m_DebugDevice)
    {
        m_DebugDevice->Release();
        m_DebugDevice = nullptr;
    }
#endif
}

void Renderer::InitFrameBuffer()
{
    m_CurrentBuffer = m_Swapchain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = s_BackbufferCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)));

        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < s_BackbufferCount; n++)
        {
            ThrowIfFailed(m_Swapchain->GetBuffer(n, IID_PPV_ARGS(&m_RenderTargets[n])));
            m_Device->CreateRenderTargetView(m_RenderTargets[n], nullptr, rtvHandle);
            rtvHandle.ptr += (1 * m_RtvDescriptorSize);
        }
    }
}

void Renderer::DestroyFrameBuffer()
{
    for (size_t i = 0; i < s_BackbufferCount; ++i)
    {
        if (m_RenderTargets[i])
        {
            m_RenderTargets[i]->Release();
            m_RenderTargets[i] = 0;
        }
    }
    if (m_RtvHeap)
    {
        m_RtvHeap->Release();
        m_RtvHeap = nullptr;
    }
}

void Renderer::InitializeResources()
{
    // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If
        // CheckFeatureSupport succeeds, the HighestVersion returned will not be
        // greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

        D3D12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        ranges[0].NumDescriptors = 1;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

        D3D12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rootSignatureDesc.Desc_1_1.NumParameters = 1;
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;

        ID3DBlob* signature;
        ID3DBlob* error;
        try
        {
            ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
            ThrowIfFailed(m_Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));
            m_RootSignature->SetName(L"Hello Triangle Root Signature");
        }
        catch (std::exception e)
        {
            const char* errStr = (const char*)error->GetBufferPointer();
            std::cout << errStr;
            error->Release();
            error = nullptr;
        }

        if (signature)
        {
            signature->Release();
            signature = nullptr;
        }
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ID3DBlob* vertexShader = nullptr;
        ID3DBlob* pixelShader = nullptr;
        ID3DBlob* errors = nullptr;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        std::string path = "";
        char pBuf[1024];

        _getcwd(pBuf, 1024);
        path = pBuf;
        path += "\\";
        std::wstring wpath = std::wstring(path.begin(), path.end());

        std::string vertCompiledPath = path, fragCompiledPath = path;
        vertCompiledPath += "assets\\triangle.vert.dxbc";
        fragCompiledPath += "assets\\triangle.frag.dxbc";

#define COMPILESHADERS
#ifdef COMPILESHADERS
        std::wstring vertPath = wpath + L"assets\\triangle.vert.hlsl";
        std::wstring fragPath = wpath + L"assets\\triangle.frag.hlsl";

        try
        {
            ThrowIfFailed(D3DCompileFromFile(vertPath.c_str(), nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vertexShader, &errors));
            ThrowIfFailed(D3DCompileFromFile(fragPath.c_str(), nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pixelShader, &errors));
        }
        catch (std::exception e)
        {
            const char* errStr = (const char*)errors->GetBufferPointer();
            std::cout << errStr;
            errors->Release();
            errors = nullptr;
        }

        std::ofstream vsOut(vertCompiledPath, std::ios::out | std::ios::binary),
            fsOut(fragCompiledPath, std::ios::out | std::ios::binary);

        vsOut.write((const char*)vertexShader->GetBufferPointer(), vertexShader->GetBufferSize());
        fsOut.write((const char*)pixelShader->GetBufferPointer(), pixelShader->GetBufferSize());

#else
        std::vector<char> vsBytecodeData = readFile(vertCompiledPath);
        std::vector<char> fsBytecodeData = readFile(fragCompiledPath);

#endif
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Create the UBO.
        {
            // Note: using upload heaps to transfer static data like vert
            // buffers is not recommended. Every time the GPU needs it, the
            // upload heap will be marshalled over. Please read up on Default
            // Heap usage. An upload heap is used here for code simplicity and
            // because there are very few verts to actually transfer.
            D3D12_HEAP_PROPERTIES heapProps;
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.NumDescriptors = 1;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            ThrowIfFailed(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UniformBufferHeap)));

            D3D12_RESOURCE_DESC uboResourceDesc;
            uboResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uboResourceDesc.Alignment = 0;
            uboResourceDesc.Width = (sizeof(UboVS) + 255) & ~255;
            uboResourceDesc.Height = 1;
            uboResourceDesc.DepthOrArraySize = 1;
            uboResourceDesc.MipLevels = 1;
            uboResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            uboResourceDesc.SampleDesc.Count = 1;
            uboResourceDesc.SampleDesc.Quality = 0;
            uboResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            uboResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            ThrowIfFailed(m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &uboResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_UniformBuffer)));
            m_UniformBufferHeap->SetName(L"Constant Buffer Upload Resource Heap");

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = m_UniformBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = (sizeof(UboVS) + 255) & ~255; // CB size is required to be 256-byte aligned.

            D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_UniformBufferHeap->GetCPUDescriptorHandleForHeapStart());
            cbvHandle.ptr = cbvHandle.ptr + m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 0;

            m_Device->CreateConstantBufferView(&cbvDesc, cbvHandle);

            // We do not intend to read from this resource on the CPU. (End is
            // less than or equal to begin)
            D3D12_RANGE readRange;
            readRange.Begin = 0;
            readRange.End = 0;

            ThrowIfFailed(m_UniformBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_MappedUniformBuffer)));
            memcpy(m_MappedUniformBuffer, &UboVS, sizeof(UboVS));
            m_UniformBuffer->Unmap(0, &readRange);
        }

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
        psoDesc.pRootSignature = m_RootSignature;

        D3D12_SHADER_BYTECODE vsBytecode;
        D3D12_SHADER_BYTECODE psBytecode;

#ifdef COMPILESHADERS
        vsBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
        vsBytecode.BytecodeLength = vertexShader->GetBufferSize();

        psBytecode.pShaderBytecode = pixelShader->GetBufferPointer();
        psBytecode.BytecodeLength = pixelShader->GetBufferSize();
#else
        vsBytecode.pShaderBytecode = vsBytecodeData.data();
        vsBytecode.BytecodeLength = vsBytecodeData.size();

        psBytecode.pShaderBytecode = fsBytecodeData.data();
        psBytecode.BytecodeLength = fsBytecodeData.size();
#endif

        psoDesc.VS = vsBytecode;
        psoDesc.PS = psBytecode;

        D3D12_RASTERIZER_DESC rasterDesc;
        rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = FALSE;
        rasterDesc.AntialiasedLineEnable = FALSE;
        rasterDesc.ForcedSampleCount = 0;
        rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        psoDesc.RasterizerState = rasterDesc;

        D3D12_BLEND_DESC blendDesc;
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
            FALSE,
            FALSE,
            D3D12_BLEND_ONE,
            D3D12_BLEND_ZERO,
            D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE,
            D3D12_BLEND_ZERO,
            D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        
        try
        {
            ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)));
        }
        catch (std::exception e)
        {
            std::cout << "Failed to create Graphics Pipeline!";
        }

        if (vertexShader)
        {
            vertexShader->Release();
            vertexShader = nullptr;
        }

        if (pixelShader)
        {
            pixelShader->Release();
            pixelShader = nullptr;
        }
    }

    CreateCommands();

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_CommandList->Close());

    // Create the vertex buffer.
    {
        const UINT vertexBufferSize = sizeof(m_VertexBufferData);

        // Note: using upload heaps to transfer static data like vert buffers is
        // not recommended. Every time the GPU needs it, the upload heap will be
        // marshalled over. Please read up on Default Heap usage. An upload heap
        // is used here for code simplicity and because there are very few verts
        // to actually transfer.
        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vertexBufferResourceDesc;
        vertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vertexBufferResourceDesc.Alignment = 0;
        vertexBufferResourceDesc.Width = vertexBufferSize;
        vertexBufferResourceDesc.Height = 1;
        vertexBufferResourceDesc.DepthOrArraySize = 1;
        vertexBufferResourceDesc.MipLevels = 1;
        vertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        vertexBufferResourceDesc.SampleDesc.Count = 1;
        vertexBufferResourceDesc.SampleDesc.Quality = 0;
        vertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_VertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;

        // We do not intend to read from this resource on the CPU.
        D3D12_RANGE readRange;
        readRange.Begin = 0;
        readRange.End = 0;

        ThrowIfFailed(m_VertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, m_VertexBufferData, sizeof(m_VertexBufferData));
        m_VertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
        m_VertexBufferView.StrideInBytes = sizeof(Vertex);
        m_VertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create the index buffer.
    {
        const UINT indexBufferSize = sizeof(m_IndexBufferData);

        // Note: using upload heaps to transfer static data like vert buffers is
        // not recommended. Every time the GPU needs it, the upload heap will be
        // marshalled over. Please read up on Default Heap usage. An upload heap
        // is used here for code simplicity and because there are very few verts
        // to actually transfer.
        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vertexBufferResourceDesc;
        vertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vertexBufferResourceDesc.Alignment = 0;
        vertexBufferResourceDesc.Width = indexBufferSize;
        vertexBufferResourceDesc.Height = 1;
        vertexBufferResourceDesc.DepthOrArraySize = 1;
        vertexBufferResourceDesc.MipLevels = 1;
        vertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        vertexBufferResourceDesc.SampleDesc.Count = 1;
        vertexBufferResourceDesc.SampleDesc.Quality = 0;
        vertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_IndexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;

        // We do not intend to read from this resource on the CPU.
        D3D12_RANGE readRange;
        readRange.Begin = 0;
        readRange.End = 0;

        ThrowIfFailed(m_IndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, m_IndexBufferData, sizeof(m_IndexBufferData));
        m_IndexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
        m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        m_IndexBufferView.SizeInBytes = indexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded
    // to the GPU.
    {
        m_FenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        
        if (m_FenceEvent == nullptr)
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        // Signal and increment the fence value.
        const UINT64 fence = m_FenceValue;
        ThrowIfFailed(m_CommandQueue->Signal(m_Fence, fence));
        m_FenceValue++;

        // Wait until the previous frame is finished.
        if (m_Fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }

        m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
    }
}

void Renderer::DestroyResources()
{
    // Sync
    CloseHandle(m_FenceEvent);

    if (m_PipelineState)
    {
        m_PipelineState->Release();
        m_PipelineState = nullptr;
    }

    if (m_RootSignature)
    {
        m_RootSignature->Release();
        m_RootSignature = nullptr;
    }

    if (m_VertexBuffer)
    {
        m_VertexBuffer->Release();
        m_VertexBuffer = nullptr;
    }

    if (m_IndexBuffer)
    {
        m_IndexBuffer->Release();
        m_IndexBuffer = nullptr;
    }

    if (m_UniformBuffer)
    {
        m_UniformBuffer->Release();
        m_UniformBuffer = nullptr;
    }

    if (m_UniformBufferHeap)
    {
        m_UniformBufferHeap->Release();
        m_UniformBufferHeap = nullptr;
    }
}

void Renderer::CreateCommands()
{
    // Create the command list.
    ThrowIfFailed(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator, m_PipelineState, IID_PPV_ARGS(&m_CommandList)));
    m_CommandList->SetName(L"Hello Triangle Command List");
}

void Renderer::SetupCommands()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_CommandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    ThrowIfFailed(m_CommandList->Reset(m_CommandAllocator, m_PipelineState));

    // Set necessary state.
    m_CommandList->SetGraphicsRootSignature(m_RootSignature);
    m_CommandList->RSSetViewports(1, &m_Viewport);
    m_CommandList->RSSetScissorRects(1, &m_SurfaceSize);

    ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_UniformBufferHeap };
    m_CommandList->SetDescriptorHeaps(_countof(pDescriptorHeaps), pDescriptorHeaps);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle(m_UniformBufferHeap->GetGPUDescriptorHandleForHeapStart());
    m_CommandList->SetGraphicsRootDescriptorTable(0, srvHandle);

    // Indicate that the back buffer will be used as a render target.
    D3D12_RESOURCE_BARRIER renderTargetBarrier;
    renderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    renderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    renderTargetBarrier.Transition.pResource = m_RenderTargets[m_FrameIndex];
    renderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    renderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    renderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_CommandList->ResourceBarrier(1, &renderTargetBarrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.ptr = rtvHandle.ptr + (m_FrameIndex * m_RtvDescriptorSize);
    m_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    m_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
    m_CommandList->IASetIndexBuffer(&m_IndexBufferView);

    m_CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);

    // Indicate that the back buffer will now be used to present.
    D3D12_RESOURCE_BARRIER presentBarrier;
    presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    presentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    presentBarrier.Transition.pResource = m_RenderTargets[m_FrameIndex];
    presentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_CommandList->ResourceBarrier(1, &presentBarrier);

    ThrowIfFailed(m_CommandList->Close());
}

void Renderer::DestroyCommands()
{
    if (m_CommandList)
    {
        m_CommandList->Reset(m_CommandAllocator, m_PipelineState);
        m_CommandList->ClearState(m_PipelineState);
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* ppCommandLists[] = { m_CommandList };
        m_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // Wait for GPU to finish work
        const UINT64 fence = m_FenceValue;
        ThrowIfFailed(m_CommandQueue->Signal(m_Fence, fence));
        m_FenceValue++;
        
        if (m_Fence->GetCompletedValue() < fence)
        {
            ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }

        m_CommandList->Release();
        m_CommandList = nullptr;
    }
}

void Renderer::SetupSwapchain(unsigned width, unsigned height)
{
    m_SurfaceSize.left = 0;
    m_SurfaceSize.top = 0;
    m_SurfaceSize.right = static_cast<LONG>(m_Width);
    m_SurfaceSize.bottom = static_cast<LONG>(m_Height);

    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;
    m_Viewport.Width = static_cast<float>(m_Width);
    m_Viewport.Height = static_cast<float>(m_Height);
    m_Viewport.MinDepth = .1f;
    m_Viewport.MaxDepth = 1000.f;

    // Update Uniforms
    float zoom = 2.5f;

    // Update matrices
    UboVS.ProjectionMatrix = glm::perspective(45.0f, (float)m_Width / (float)m_Height, 0.01f, 1024.0f);

    UboVS.ViewMatrix = glm::translate(glm::identity<mat4>(), vec3(0.0f, 0.0f, zoom));

    UboVS.ModelMatrix = glm::identity<mat4>();

    if (m_Swapchain != nullptr)
    {
        m_Swapchain->ResizeBuffers(s_BackbufferCount, m_Width, m_Height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.BufferCount = s_BackbufferCount;
        swapchainDesc.Width = width;
        swapchainDesc.Height = height;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.SampleDesc.Count = 1;

        IDXGISwapChain1* swapchain = xgfx::createSwapchain(m_Window, m_Factory, m_CommandQueue, &swapchainDesc);
        HRESULT swapchainSupport = swapchain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&swapchain);

        if (SUCCEEDED(swapchainSupport))
            m_Swapchain = (IDXGISwapChain3*)swapchain;
    }
    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
}

void Renderer::Resize(unsigned width, unsigned height)
{
    m_Width = clamp(width, 1u, 0xffffu);
    m_Height = clamp(height, 1u, 0xffffu);

    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The
    // D3D12HelloFrameBuffering sample illustrates how to use fences for
    // efficient resource usage and to maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_FenceValue;
    ThrowIfFailed(m_CommandQueue->Signal(m_Fence, fence));
    m_FenceValue++;

    // Wait until the previous frame is finished.
    if (m_Fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
        WaitForSingleObjectEx(m_FenceEvent, INFINITE, false);
    }

    DestroyFrameBuffer();
    SetupSwapchain(width, height);
    InitFrameBuffer();
}

void Renderer::Render()
{
    // Framelimit set to 60 fps
    m_EndTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::milli>(m_EndTime - m_StartTime).count();
    
    if (time < (1000.0f / 60.0f))
        return;
    
    m_StartTime = std::chrono::high_resolution_clock::now();

    {
        // Update Uniforms
        m_ElapsedTime += 0.001f * time;
        m_ElapsedTime = fmodf(m_ElapsedTime, 6.283185307179586f);

        UboVS.ModelMatrix = glm::rotate(UboVS.ModelMatrix, 0.001f * time, vec3(0.0f, 1.0f, 0.0f));

        D3D12_RANGE readRange;
        readRange.Begin = 0;
        readRange.End = 0;

        ThrowIfFailed(m_UniformBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_MappedUniformBuffer)));
        memcpy(m_MappedUniformBuffer, &UboVS, sizeof(UboVS));
        m_UniformBuffer->Unmap(0, &readRange);
    }

    // Record all the commands we need to render the scene into the command
    // list.
    SetupCommands();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_CommandList };
    m_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    m_Swapchain->Present(1, 0);

    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.

    // Signal and increment the fence value.
    const UINT64 fence = m_FenceValue;
    ThrowIfFailed(m_CommandQueue->Signal(m_Fence, fence));
    m_FenceValue++;

    // Wait until the previous frame is finished.
    if (m_Fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }

    m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
}