#pragma once

#include "CrossWindow/CrossWindow.h"
#include "CrossWindow/Graphics.h"

#define GLM_FORCE_SSE42 1
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES 1
#define GLM_FORCE_LEFT_HANDED
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

#include <direct.h>

// Common Utils

inline std::vector<char> ReadFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    bool exists = (bool)file;

    if (!exists || !file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
};

// Renderer

class Renderer
{
  public:
    Renderer(xwin::Window& window);

    ~Renderer();

    // Render onto the render target
    void Render();

    // Resize the window and internal data structures
    void Resize(unsigned width, unsigned height);

  protected:
    // Initialize your Graphics API
    void InitializeAPI(xwin::Window& window);

    // Destroy any Graphics API data structures used in this example
    void DestroyAPI();

    // Initialize any resources such as VBOs, IBOs, used in this example
    void InitializeResources();

    // Destroy any resources used in this example
    void DestroyResources();

    // Create graphics API specific data structures to send commands to the GPU
    void CreateCommands();

    // Set up commands used when rendering frame by this app
    void SetupCommands();

    // Destroy all commands
    void DestroyCommands();

    // Set up the FrameBuffer
    void InitFrameBuffer();

    void DestroyFrameBuffer();

    // Set up the RenderPass
    void CreateRenderPass();

    void CreateSynchronization();

    // Set up the swapchain
    void SetupSwapchain(unsigned width, unsigned height);

    struct Vertex
    {
        float Position[3];
        float Color[3];
    };

    Vertex m_VertexBufferData[3] = {
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { {  0.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };

    uint32_t m_IndexBufferData[3] = {0, 1, 2};

    std::chrono::time_point<std::chrono::steady_clock> m_StartTime, m_EndTime;
    float m_ElapsedTime = 0.0f;

    // Uniform data
    struct
    {
        glm::mat4 ProjectionMatrix;
        glm::mat4 ModelMatrix;
        glm::mat4 ViewMatrix;
    } UboVS;

    static const UINT s_BackbufferCount = 2;

    xwin::Window* m_Window;
    unsigned m_Width, m_Height;

    // Initialization
    IDXGIFactory4* m_Factory;
    IDXGIAdapter1* m_Adapter;
#if defined(_DEBUG)
    ID3D12Debug1* m_DebugController;
    ID3D12DebugDevice* m_DebugDevice;
#endif
    ID3D12Device* m_Device;
    ID3D12CommandQueue* m_CommandQueue;
    ID3D12CommandAllocator* m_CommandAllocator;
    ID3D12GraphicsCommandList* m_CommandList;

    // Current Frame
    UINT m_CurrentBuffer;
    ID3D12DescriptorHeap* m_RtvHeap;
    ID3D12Resource* m_RenderTargets[s_BackbufferCount];
    IDXGISwapChain3* m_Swapchain;

    // Resources
    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_SurfaceSize;

    ID3D12Resource* m_VertexBuffer;
    ID3D12Resource* m_IndexBuffer;

    ID3D12Resource* m_UniformBuffer;
    ID3D12DescriptorHeap* m_UniformBufferHeap;
    UINT8* m_MappedUniformBuffer;

    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

    UINT m_RtvDescriptorSize;
    ID3D12RootSignature* m_RootSignature;
    ID3D12PipelineState* m_PipelineState;

    // Sync
    UINT m_FrameIndex;
    HANDLE m_FenceEvent;
    ID3D12Fence* m_Fence;
    UINT64 m_FenceValue;
};