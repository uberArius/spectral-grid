#pragma once
#include "WinUtil.h"
#include "GridSimulation.h"
#include "Camera.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <filesystem>
namespace sg {
struct PixelStats { unsigned fullYellowPixels=0, litPixels=0, premultipliedViolations=0; float maxRed=0,maxAlpha=0; };
class Renderer {
public:
    void initialize(HWND hwnd);
    void shutdown();
    void resize(unsigned width,unsigned height);
    void draw(const Settings& settings,GridSimulation& grid,const Camera& camera,bool clearBackground=true);
    HRESULT present(bool vsync);
    void screenshot(const std::filesystem::path& path);
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    float backgroundAlpha();
    PixelStats pixelStats();
    std::string adapterName() const;
    void verifyLight();
    void verifyGravity(); // Diagnostic CPU/GPU comparison; never runs in normal mode.
private:
    void createTarget();
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<IDCompositionDevice> composition_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11Texture2D> history_;
    ComPtr<ID3D11ShaderResourceView> historyView_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depth_;
    unsigned width_=1,height_=1;
};
}
