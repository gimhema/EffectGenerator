using System;
using System.Numerics;
using System.Runtime.InteropServices;

using Vortice.D3DCompiler;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DXGI;
using Vortice.Mathematics;

using static Vortice.Direct3D11.D3D11;

namespace EffectEditor
{
    public sealed class D3D11EffectPreviewer : IDisposable
    {
        public static readonly string DefaultHlsl = @"
cbuffer Globals : register(b0)
{
    float4 gTimeRes; // x=time, y=width, z=height, w=unused
};

struct VSIn
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = float4(v.pos, 0, 1);
    o.uv = v.uv;
    return o;
}

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return frac(p.x * p.y);
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float t = gTimeRes.x;
    float2 res = float2(gTimeRes.y, gTimeRes.z);

    float2 p = i.uv * res;
    float n = hash21(p * 0.02 + t);

    float3 col = float3(i.uv.x, i.uv.y, 0.5 + 0.5 * sin(t));
    col += 0.15 * (n - 0.5);

    return float4(col, 1);
}
";

        private readonly IntPtr _hwnd;

        private ID3D11Device _device = null!;
        private ID3D11DeviceContext _ctx = null!;
        private IDXGISwapChain1 _swapChain = null!;
        private ID3D11RenderTargetView _rtv = null!;

        private ID3D11VertexShader _vs = null!;
        private ID3D11PixelShader _ps = null!;
        private ID3D11InputLayout _inputLayout = null!;
        private ID3D11Buffer _vb = null!;
        private ID3D11Buffer _cb = null!;

        private int _width;
        private int _height;
        private readonly DateTime _start = DateTime.UtcNow;

        private string _hlsl = DefaultHlsl;

        [StructLayout(LayoutKind.Sequential)]
        private struct Vertex
        {
            public Vector2 Pos;
            public Vector2 UV;
            public Vertex(Vector2 pos, Vector2 uv) { Pos = pos; UV = uv; }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct GlobalsCB
        {
            public Vector4 Time_Resolution; // x=time, y=width, z=height, w=unused
        }

        public D3D11EffectPreviewer(IntPtr hwnd, int width, int height)
        {
            _hwnd = hwnd;
            _width = Math.Max(1, width);
            _height = Math.Max(1, height);

            CreateDeviceAndSwapChain();
            CreateBackBufferRTV();
            CreateFullscreenTriangle();
            CreateConstantBuffer();
            CompileAndCreateShaders(); // 기본 셰이더 1회
            SetViewport();
        }

        public void SetShaderSource(string hlsl)
        {
            _hlsl = hlsl ?? "";
        }

        private void CreateDeviceAndSwapChain()
        {
            D3D11CreateDevice(
                adapter: (IDXGIAdapter?)null,
                driverType: DriverType.Hardware,
                flags: DeviceCreationFlags.BgraSupport,
                featureLevels: (FeatureLevel[]?)null,
                device: out _device,
                featureLevel: out _,
                immediateContext: out _ctx
            );

            using var dxgiDevice = _device.QueryInterface<IDXGIDevice>();
            using var adapter = dxgiDevice.GetAdapter();
            using var factory = adapter.GetParent<IDXGIFactory2>();

            var swapDesc = new SwapChainDescription1
            {
                Width = (uint)_width,
                Height = (uint)_height,
                Format = Format.R8G8B8A8_UNorm,
                Stereo = false,
                SampleDescription = new SampleDescription(1, 0),
                BufferUsage = Usage.RenderTargetOutput,
                BufferCount = 2,
                Scaling = Scaling.None,
                SwapEffect = SwapEffect.FlipDiscard,
                AlphaMode = AlphaMode.Ignore,
                Flags = SwapChainFlags.None
            };

            _swapChain = factory.CreateSwapChainForHwnd(_device, _hwnd, swapDesc);
            factory.MakeWindowAssociation(_hwnd, WindowAssociationFlags.IgnoreAltEnter);
        }

        private void CreateBackBufferRTV()
        {
            _rtv?.Dispose();
            using var backBuffer = _swapChain.GetBuffer<ID3D11Texture2D>(0);
            _rtv = _device.CreateRenderTargetView(backBuffer);
        }

        private void CreateFullscreenTriangle()
        {
            var verts = new Vertex[]
            {
                new Vertex(new Vector2(-1, -1), new Vector2(0, 1)),
                new Vertex(new Vector2(-1,  3), new Vector2(0, -1)),
                new Vertex(new Vector2( 3, -1), new Vector2(2, 1)),
            };

            var desc = new BufferDescription(
                (uint)(Marshal.SizeOf<Vertex>() * verts.Length),
                BindFlags.VertexBuffer,
                ResourceUsage.Immutable,
                CpuAccessFlags.None,
                ResourceOptionFlags.None,
                0
            );

            var handle = GCHandle.Alloc(verts, GCHandleType.Pinned);
            try
            {
                var initData = new SubresourceData(handle.AddrOfPinnedObject());
                _vb = _device.CreateBuffer(desc, initData);
            }
            finally
            {
                handle.Free();
            }
        }

        private void CreateConstantBuffer()
        {
            var desc = new BufferDescription(
                (uint)Marshal.SizeOf<GlobalsCB>(),
                BindFlags.ConstantBuffer,
                ResourceUsage.Dynamic,
                CpuAccessFlags.Write,
                ResourceOptionFlags.None,
                0
            );

            _cb = _device.CreateBuffer(desc);
        }

        public void RecompileShaders()
        {
            CompileAndCreateShaders();
        }

        private void CompileAndCreateShaders()
        {
            _vs?.Dispose();
            _ps?.Dispose();
            _inputLayout?.Dispose();

            // 네 환경: Compile 시그니처 = (source, entry, sourceName, profile)
            ReadOnlyMemory<byte> vsMem = Compiler.Compile(_hlsl, "VSMain", "editor.hlsl", "vs_5_0");
            ReadOnlyMemory<byte> psMem = Compiler.Compile(_hlsl, "PSMain", "editor.hlsl", "ps_5_0");

            byte[] vsBytes = vsMem.ToArray();
            byte[] psBytes = psMem.ToArray();

            _vs = _device.CreateVertexShader(vsBytes);
            _ps = _device.CreatePixelShader(psBytes);

            var elements = new[]
            {
                new InputElementDescription("POSITION", 0, Format.R32G32_Float, 0, 0),
                new InputElementDescription("TEXCOORD", 0, Format.R32G32_Float, 8, 0),
            };

            _inputLayout = _device.CreateInputLayout(elements, vsBytes);
        }

        private void SetViewport()
        {
            _ctx.RSSetViewport(new Viewport(0, 0, _width, _height, 0, 1));
        }

        public void Resize(int width, int height)
        {
            _width = Math.Max(1, width);
            _height = Math.Max(1, height);

            _rtv?.Dispose();
            _swapChain.ResizeBuffers(0, (uint)_width, (uint)_height, Format.Unknown, SwapChainFlags.None);

            CreateBackBufferRTV();
            SetViewport();
        }

        public void Render()
        {
            float t = (float)(DateTime.UtcNow - _start).TotalSeconds;

            var mapped = _ctx.Map(_cb, 0, MapMode.WriteDiscard, Vortice.Direct3D11.MapFlags.None);
            var data = new GlobalsCB
            {
                Time_Resolution = new Vector4(t, _width, _height, 0)
            };
            Marshal.StructureToPtr(data, mapped.DataPointer, false);
            _ctx.Unmap(_cb, 0);

            _ctx.OMSetRenderTargets(_rtv);
            _ctx.ClearRenderTargetView(_rtv, new Color4(0, 0, 0, 1));

            _ctx.IASetInputLayout(_inputLayout);
            _ctx.IASetPrimitiveTopology(PrimitiveTopology.TriangleList);

            uint stride = (uint)Marshal.SizeOf<Vertex>();
            _ctx.IASetVertexBuffers(0, new[] { _vb }, new[] { stride }, new[] { 0u });

            _ctx.VSSetShader(_vs);
            _ctx.PSSetShader(_ps);

            _ctx.VSSetConstantBuffer(0, _cb);
            _ctx.PSSetConstantBuffer(0, _cb);

            _ctx.Draw(3, 0);
            _swapChain.Present(1, PresentFlags.None);
        }

        public void Dispose()
        {
            _cb?.Dispose();
            _vb?.Dispose();
            _inputLayout?.Dispose();
            _ps?.Dispose();
            _vs?.Dispose();
            _rtv?.Dispose();
            _swapChain?.Dispose();
            _ctx?.Dispose();
            _device?.Dispose();
        }
    }
}
