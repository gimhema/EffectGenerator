
// 빌드 설정:
// - 프로젝트 형식: Win32 / Windows Subsystem
// - 링커 Input: d3d11.lib, d3dcompiler.lib
//
// 기능 요약:
// - Win32 창 생성
// - D3D11 디바이스 / 스왑체인 초기화
// - 중앙에 사각형 하나를 그리고, Pixel Shader에서 시간 기반 플레임(불꽃) 비슷한 패턴 표현
// - Additive Blending 적용으로 불빛 느낌

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// 전역 변수
HWND                    g_hWnd = NULL;
ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pImmediateContext = NULL;
IDXGISwapChain* g_pSwapChain = NULL;
ID3D11RenderTargetView* g_pRenderTargetView = NULL;
ID3D11VertexShader* g_pVertexShader = NULL;
ID3D11PixelShader* g_pPixelShader = NULL;
ID3D11InputLayout* g_pInputLayout = NULL;
ID3D11Buffer* g_pVertexBuffer = NULL;
ID3D11Buffer* g_pConstantBuffer = NULL;
ID3D11BlendState* g_pBlendState = NULL;

float                   g_AspectRatio = 1.0f;
float                   g_Time = 0.0f;

// 정점 구조체
struct SimpleVertex
{
    float x, y, z;
    float u, v;
};

// 상수 버퍼 구조체
struct CBChangesEveryFrame
{
    float gTime;
    float pad[3]; // 16바이트 정렬용 패딩
};

// 함수 선언
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
HRESULT             InitD3D();
void                CleanupD3D();
void                Render();
HRESULT             CreateRenderTargetView();
void                SetViewport(UINT width, UINT height);

// HLSL 코드 (VS/PS)
const char* g_VSCode =
"struct VS_IN                                         \n"
"{                                                   \n"
"    float3 pos : POSITION;                          \n"
"    float2 uv  : TEXCOORD0;                         \n"
"};                                                  \n"
"                                                    \n"
"struct VS_OUT                                        \n"
"{                                                   \n"
"    float4 pos : SV_POSITION;                       \n"
"    float2 uv  : TEXCOORD0;                         \n"
"};                                                  \n"
"                                                    \n"
"VS_OUT main(VS_IN input)                            \n"
"{                                                   \n"
"    VS_OUT o;                                       \n"
"    o.pos = float4(input.pos, 1.0f);                \n"
"    o.uv  = input.uv;                               \n"
"    return o;                                       \n"
"}                                                   \n";

const char* g_PSCode =
"struct VS_OUT                                        \n"
"{                                                   \n"
"    float4 pos : SV_POSITION;                       \n"
"    float2 uv  : TEXCOORD0;                         \n"
"};                                                  \n"
"                                                    \n"
"cbuffer CB : register(b0)                           \n"
"{                                                   \n"
"    float gTime;                                    \n"
"    float3 padding;                                 \n"
"};                                                  \n"
"                                                    \n"
"float rand2d(float2 co)                             \n"
"{                                                   \n"
"    return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453); \n"
"}                                                   \n"
"                                                    \n"
"float4 main(VS_OUT input) : SV_TARGET               \n"
"{                                                   \n"
"    float2 uv = input.uv;                           \n"
"                                                    \n"
"    // 가운데 쪽이 더 뜨겁게 보이도록 X축 기준 중앙에서의 거리 사용   \n"
"    float centerDist = abs(uv.x - 0.5f);            \n"
"    float centerFalloff = 1.0f - saturate(centerDist * 2.0f);        \n"
"                                                    \n"
"    // 노이즈로 깜빡이는 플레임 효과                                      \n"
"    float2 noiseCoord = uv * (10.0f + sin(gTime * 2.0f) * 5.0f) + gTime; \n"
"    float noise = rand2d(noiseCoord);               \n"
"                                                    \n"
"    // 위로 갈수록 강해지는 기본 불꽃 + 노이즈                                \n"
"    float base = uv.y * 2.0f;                       \n"
"    float intensity = saturate(base * centerFalloff + noise * 1.3f - (1.0f - uv.y)); \n"
"                                                    \n"
"    // 색 그라데이션: 아래쪽은 붉고, 위로 갈수록 노란/흰색                      \n"
"    float3 colHot   = float3(1.0f, 0.3f, 0.0f);     \n"
"    float3 colWarm = float3(1.0f, 0.8f, 0.4f);      \n"
"    float3 col = lerp(colHot, colWarm, uv.y);       \n"
"                                                    \n"
"    col *= intensity;                               \n"
"    float alpha = intensity;                        \n"
"                                                    \n"
"    return float4(col, alpha);                      \n"
"}                                                   \n";

// WinMain
int APIENTRY WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 윈도우 클래스 등록
    WNDCLASSEX wcex;
    ZeroMemory(&wcex, sizeof(WNDCLASSEX));
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"DX11FlameWindowClass";


    if (!RegisterClassEx(&wcex))
        return 0;

    RECT rc = { 0, 0, 1280, 720 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindow(L"DX11FlameWindowClass", L"DX11 Flame/Laser Effect Example",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd)
        return 0;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    if (FAILED(InitD3D()))
    {
        CleanupD3D();
        return 0;
    }

    // 메시지 루프
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // 간단히 frame당 고정 dt 사용 (약 60fps 가정)
            g_Time += 0.016f;
            Render();
        }
    }

    CleanupD3D();
    return (int)msg.wParam;
}

// Direct3D 초기화
HRESULT InitD3D()
{
    // 스왑체인/디바이스 생성
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;
    if (height == 0) height = 1;
    g_AspectRatio = (float)width / (float)height;

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevelsRequested[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevelCreated;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        createDeviceFlags,
        featureLevelsRequested,
        sizeof(featureLevelsRequested) / sizeof(D3D_FEATURE_LEVEL),
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevelCreated,
        &g_pImmediateContext
    );

    if (FAILED(hr))
        return hr;

    // RenderTargetView 생성
    hr = CreateRenderTargetView();
    if (FAILED(hr))
        return hr;

    // Viewport 설정
    SetViewport(width, height);

    // 정점 버퍼 생성 (중앙에 세로 불꽃 사각형)
    SimpleVertex vertices[] =
    {
        //   x      y      z     u     v
        { -0.2f, -0.8f, 0.5f,   0.0f, 0.0f }, // 좌하
        { -0.2f,  0.8f, 0.5f,   0.0f, 1.0f }, // 좌상
        {  0.2f,  0.8f, 0.5f,   1.0f, 1.0f }, // 우상

        { -0.2f, -0.8f, 0.5f,   0.0f, 0.0f }, // 좌하
        {  0.2f,  0.8f, 0.5f,   1.0f, 1.0f }, // 우상
        {  0.2f, -0.8f, 0.5f,   1.0f, 0.0f }, // 우하
    };

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(SimpleVertex) * 6;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = vertices;

    hr = g_pd3dDevice->CreateBuffer(&bd, &InitData, &g_pVertexBuffer);
    if (FAILED(hr))
        return hr;

    // HLSL 셰이더 컴파일
    ID3DBlob* pVSBlob = NULL;
    ID3DBlob* pPSBlob = NULL;
    ID3DBlob* pErrorBlob = NULL;

    // Vertex Shader
    hr = D3DCompile(
        g_VSCode, strlen(g_VSCode),
        NULL, NULL, NULL,
        "main", "vs_5_0",
        0, 0, &pVSBlob, &pErrorBlob
    );
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return hr;
    }

    hr = g_pd3dDevice->CreateVertexShader(
        pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        NULL,
        &g_pVertexShader
    );
    if (FAILED(hr))
    {
        pVSBlob->Release();
        return hr;
    }

    // Input Layout
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT   , 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_pd3dDevice->CreateInputLayout(
        layoutDesc, 2,
        pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        &g_pInputLayout
    );
    pVSBlob->Release();
    if (FAILED(hr))
        return hr;

    // Pixel Shader
    hr = D3DCompile(
        g_PSCode, strlen(g_PSCode),
        NULL, NULL, NULL,
        "main", "ps_5_0",
        0, 0, &pPSBlob, &pErrorBlob
    );
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return hr;
    }

    hr = g_pd3dDevice->CreatePixelShader(
        pPSBlob->GetBufferPointer(),
        pPSBlob->GetBufferSize(),
        NULL,
        &g_pPixelShader
    );
    pPSBlob->Release();
    if (FAILED(hr))
        return hr;

    // 상수 버퍼 (시간)
    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory(&cbDesc, sizeof(cbDesc));
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(CBChangesEveryFrame);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = 0;

    hr = g_pd3dDevice->CreateBuffer(&cbDesc, NULL, &g_pConstantBuffer);
    if (FAILED(hr))
        return hr;

    // BlendState (Additive Blending)
    D3D11_BLEND_DESC blendDesc;
    ZeroMemory(&blendDesc, sizeof(blendDesc));
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    D3D11_RENDER_TARGET_BLEND_DESC rtbd;
    ZeroMemory(&rtbd, sizeof(rtbd));
    rtbd.BlendEnable = TRUE;
    rtbd.SrcBlend = D3D11_BLEND_ONE;
    rtbd.DestBlend = D3D11_BLEND_ONE;
    rtbd.BlendOp = D3D11_BLEND_OP_ADD;
    rtbd.SrcBlendAlpha = D3D11_BLEND_ONE;
    rtbd.DestBlendAlpha = D3D11_BLEND_ONE;
    rtbd.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rtbd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    blendDesc.RenderTarget[0] = rtbd;

    hr = g_pd3dDevice->CreateBlendState(&blendDesc, &g_pBlendState);
    if (FAILED(hr))
        return hr;

    return S_OK;
}

// RenderTargetView 생성
HRESULT CreateRenderTargetView()
{
    HRESULT hr;
    ID3D11Texture2D* pBackBuffer = NULL;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr))
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_pRenderTargetView);
    pBackBuffer->Release();

    return hr;
}

// Viewport 설정
void SetViewport(UINT width, UINT height)
{
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    g_pImmediateContext->RSSetViewports(1, &vp);
}

// 렌더링
void Render()
{
    if (!g_pImmediateContext) return;

    float ClearColor[4] = { 0.02f, 0.02f, 0.05f, 1.0f }; // 거의 검은 파란 배경
    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, NULL);
    g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, ClearColor);

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    g_pImmediateContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pImmediateContext->IASetInputLayout(g_pInputLayout);

    g_pImmediateContext->VSSetShader(g_pVertexShader, NULL, 0);
    g_pImmediateContext->PSSetShader(g_pPixelShader, NULL, 0);

    // 상수 버퍼 업데이트 (시간)
    CBChangesEveryFrame cb;
    cb.gTime = g_Time;
    cb.pad[0] = cb.pad[1] = cb.pad[2] = 0.0f;
    g_pImmediateContext->UpdateSubresource(g_pConstantBuffer, 0, NULL, &cb, 0, 0);

    g_pImmediateContext->VSSetConstantBuffers(0, 1, &g_pConstantBuffer);
    g_pImmediateContext->PSSetConstantBuffers(0, 1, &g_pConstantBuffer);

    // Additive Blending 적용
    float blendFactor[4] = { 0,0,0,0 };
    UINT sampleMask = 0xffffffff;
    g_pImmediateContext->OMSetBlendState(g_pBlendState, blendFactor, sampleMask);

    g_pImmediateContext->Draw(6, 0);

    // 프레젠트
    g_pSwapChain->Present(1, 0);
}

// 정리
void CleanupD3D()
{
    if (g_pImmediateContext) g_pImmediateContext->ClearState();

    if (g_pBlendState)        g_pBlendState->Release();
    if (g_pConstantBuffer)    g_pConstantBuffer->Release();
    if (g_pVertexBuffer)      g_pVertexBuffer->Release();
    if (g_pInputLayout)       g_pInputLayout->Release();
    if (g_pPixelShader)       g_pPixelShader->Release();
    if (g_pVertexShader)      g_pVertexShader->Release();
    if (g_pRenderTargetView)  g_pRenderTargetView->Release();
    if (g_pSwapChain)         g_pSwapChain->Release();
    if (g_pImmediateContext)  g_pImmediateContext->Release();
    if (g_pd3dDevice)         g_pd3dDevice->Release();
}

// 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && g_pImmediateContext)
        {
            // 리사이즈 처리: RTV 재생성
            if (g_pRenderTargetView)
            {
                g_pRenderTargetView->Release();
                g_pRenderTargetView = NULL;
            }

            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            if (height == 0) height = 1;
            g_AspectRatio = (float)width / (float)height;

            g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (SUCCEEDED(CreateRenderTargetView()))
            {
                SetViewport(width, height);
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}
