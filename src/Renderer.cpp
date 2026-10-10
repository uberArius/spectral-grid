#include "Renderer.h"
#include "ShaderSource.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace sg {
namespace {
struct alignas(16) Scene { DirectX::XMFLOAT4X4 matrix; DirectX::XMFLOAT4 shape,grid,low,high,appearance,physics; };
ComPtr<ID3DBlob> compile(const char* entry,const char* target) {
    ComPtr<ID3DBlob> blob,error;
    auto hr=D3DCompile(pointShaderSource,sizeof(pointShaderSource)-1,"points.hlsl",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error);
    if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"HLSL compilation failed");
    return blob;
}
}
void Renderer::initialize(HWND hwnd) {
    RECT client; GetClientRect(hwnd,&client); width_=std::max(1L,client.right); height_=std::max(1L,client.bottom);
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device_,nullptr,&context_),"Direct3D 11 (hardware feature level 11.0 required)");
    ComPtr<IDXGIDevice> dxgi; check(device_.As(&dxgi),"IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"GetAdapter");
    ComPtr<IDXGIFactory2> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)),"IDXGIFactory2");
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=width_; desc.Height=height_; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
    desc.Scaling=DXGI_SCALING_STRETCH; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
    check(factory->CreateSwapChainForComposition(device_.Get(),&desc,nullptr,&swap_),"CreateSwapChainForComposition");
    check(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_)),"DCompositionCreateDevice");
    check(composition_->CreateTargetForHwnd(hwnd,TRUE,&target_),"CreateTargetForHwnd");
    check(composition_->CreateVisual(&visual_),"CreateVisual"); check(visual_->SetContent(swap_.Get()),"SetContent");
    check(target_->SetRoot(visual_.Get()),"SetRoot"); check(composition_->Commit(),"Commit");
    auto vertex=compile("VSMain","vs_5_0"),pixel=compile("PSMain","ps_5_0");
    check(device_->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_),"VertexShader");
    check(device_->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_),"PixelShader");
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Scene); cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    check(device_->CreateBuffer(&cb,nullptr,&constants_),"Constants");
    D3D11_TEXTURE2D_DESC tex{}; tex.Width=256; tex.Height=257; tex.MipLevels=tex.ArraySize=1; tex.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; tex.SampleDesc.Count=1; tex.Usage=D3D11_USAGE_DEFAULT; tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    check(device_->CreateTexture2D(&tex,nullptr,&history_),"History texture"); check(device_->CreateShaderResourceView(history_.Get(),nullptr,&historyView_),"History view");
    D3D11_BLEND_DESC b{}; auto& rt=b.RenderTarget[0]; rt.BlendEnable=TRUE; rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE; rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ONE; rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    check(device_->CreateBlendState(&b,&blend_),"Blend");
    D3D11_RASTERIZER_DESC r{}; r.FillMode=D3D11_FILL_SOLID; r.CullMode=D3D11_CULL_NONE; r.DepthClipEnable=TRUE;
    check(device_->CreateRasterizerState(&r,&raster_),"Rasterizer");
    D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable=FALSE; d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO; d.DepthFunc=D3D11_COMPARISON_ALWAYS;
    check(device_->CreateDepthStencilState(&d,&depth_),"Depth state"); createTarget();
}
void Renderer::createTarget() { ComPtr<ID3D11Texture2D> buffer; check(swap_->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Backbuffer"); check(device_->CreateRenderTargetView(buffer.Get(),nullptr,&rtv_),"RenderTarget"); }
void Renderer::shutdown() {
    if(context_) { context_->ClearState(); context_->Flush(); }
    if(visual_) visual_->SetContent(nullptr); if(target_) target_->SetRoot(nullptr); if(composition_) composition_->Commit();
    rtv_.Reset(); vs_.Reset(); ps_.Reset(); constants_.Reset(); historyView_.Reset(); history_.Reset(); blend_.Reset(); raster_.Reset(); depth_.Reset();
    visual_.Reset(); target_.Reset(); composition_.Reset(); swap_.Reset(); context_.Reset(); device_.Reset();
}
void Renderer::resize(unsigned w,unsigned h) {
    if(!swap_ || !w || !h || (width_==w && height_==h)) return;
    context_->OMSetRenderTargets(0,nullptr,nullptr); rtv_.Reset();
    check(swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"ResizeBuffers"); width_=w; height_=h; createTarget();
}
void Renderer::draw(const Settings& s,GridSimulation& grid,const Camera& camera,bool clearBackground) {
    for(int row:grid.dirtyRows()) {
        D3D11_BOX box{0,unsigned(row),0,unsigned(grid.bands()),unsigned(row+1),1};
        context_->UpdateSubresource(history_.Get(),0,&box,grid.row(row),grid.bands()*sizeof(GridPoint),0);
    }
    grid.clearDirty();
    Scene scene{}; DirectX::XMStoreFloat4x4(&scene.matrix,camera.matrix(s,float(width_)/height_));
    scene.shape={s.spacing,s.flightHeight(),s.pointSize,float(grid.simulationTime())}; scene.grid={float(grid.bands()),float(grid.columns()),float(grid.newest()),float(grid.phase())};
    scene.physics={float(grid.acceleration()),std::log(.5f)/std::log(s.colorMidpoint),grid.bounce(),0};
    scene.low={s.low[0],s.low[1],s.low[2],1}; scene.high={s.high[0],s.high[1],s.high[2],1};
    float projection=1/std::tan(DirectX::XMConvertToRadians(46)*.5f);
    scene.appearance={s.brightness,s.glowEnabled && s.effects?s.glow:0,projection*height_/width_,projection};
    D3D11_MAPPED_SUBRESOURCE map{}; check(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"Map constants"); std::memcpy(map.pData,&scene,sizeof(scene)); context_->Unmap(constants_.Get(),0);
    float a=1-s.transparency*.01f,clear[]={s.background[0]*a,s.background[1]*a,s.background[2]*a,a};
    if(clearBackground) context_->ClearRenderTargetView(rtv_.Get(),clear); context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),nullptr);
    D3D11_VIEWPORT viewport{0,0,float(width_),float(height_),0,1}; context_->RSSetViewports(1,&viewport); context_->RSSetState(raster_.Get());
    context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff); context_->OMSetDepthStencilState(depth_.Get(),0);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(),nullptr,0); context_->PSSetShader(ps_.Get(),nullptr,0); context_->GSSetShader(nullptr,nullptr,0);
    context_->VSSetConstantBuffers(0,1,constants_.GetAddressOf()); context_->PSSetConstantBuffers(0,1,constants_.GetAddressOf()); context_->VSSetShaderResources(0,1,historyView_.GetAddressOf());
    context_->DrawInstanced(6,grid.bands()*grid.rows(),0,0);
    ID3D11ShaderResourceView* empty=nullptr; context_->VSSetShaderResources(0,1,&empty);
}
HRESULT Renderer::present(bool vsync) { return swap_->Present(vsync?1:0,0); }
void Renderer::verifyLight() {
    Settings s; s.bands=s.columns=16; s.spacing=.05f; s.pointSize=.09f;
    s.low=s.high={1,1,0}; s.brightness=1;
    Camera camera; GridSimulation simulation; simulation.resize(16,16,0);
    for(float transparency:{0.0f,100.0f}) {
        s.transparency=transparency; s.low=s.high={1,1,0};
        draw(s,simulation,camera); auto before=pixelStats();
        if(!before.fullYellowPixels || before.premultipliedViolations)
            throw std::runtime_error("GPU yellow light does not reach full intensity");
        // Overlapping red points/halos must not erase existing yellow light.
        s.low=s.high={1,0,0}; draw(s,simulation,camera,false); auto after=pixelStats();
        if(after.fullYellowPixels<before.fullYellowPixels || after.premultipliedViolations)
            throw std::runtime_error("Overlapping points darken yellow light");
    }
}
void Renderer::verifyGravity() {
    constexpr unsigned count=16*17;
    auto code=compile("CSValidateGravity","cs_5_0");
    ComPtr<ID3D11ComputeShader> shader;
    check(device_->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"Gravity validation shader");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth=2*count*sizeof(float); desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride=sizeof(float);
    ComPtr<ID3D11Buffer> output,staging;
    check(device_->CreateBuffer(&desc,nullptr,&output),"Gravity validation output");
    D3D11_UNORDERED_ACCESS_VIEW_DESC view{}; view.Format=DXGI_FORMAT_UNKNOWN;
    view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; view.Buffer.NumElements=2*count;
    ComPtr<ID3D11UnorderedAccessView> uav;
    check(device_->CreateUnorderedAccessView(output.Get(),&view,&uav),"Gravity validation UAV");
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device_->CreateBuffer(&desc,nullptr,&staging),"Gravity validation staging");

    Settings settings; settings.bands=settings.columns=16;
    Camera camera; GridSimulation simulation;
    SpectrumTimeline timeline; SpectrumFrame frame; frame.bands=16;
    for(int i=0;i<16;++i) frame.values[i]=(i+1)/16.0f;
    timeline.add(frame); frame.time=100; timeline.add(frame);
    simulation.resize(16,16,0);
    const auto compare=[&](double time,float gravity) {
        settings.gravity=gravity;
        simulation.advance(time,1,gravity,timeline,settings.flightHeight(),true,settings.bounce);
        draw(settings,simulation,camera);
        context_->CSSetShader(shader.Get(),nullptr,0);
        context_->CSSetConstantBuffers(0,1,constants_.GetAddressOf());
        context_->CSSetShaderResources(0,1,historyView_.GetAddressOf());
        context_->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
        context_->Dispatch((count+63)/64,1,1);
        ID3D11UnorderedAccessView* emptyUav=nullptr;
        ID3D11ShaderResourceView* emptySrv=nullptr;
        context_->CSSetUnorderedAccessViews(0,1,&emptyUav,nullptr);
        context_->CSSetShaderResources(0,1,&emptySrv);
        context_->CSSetShader(nullptr,nullptr,0);
        context_->CopyResource(staging.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"Gravity validation readback");
        auto values=static_cast<const float*>(map.pData);
        bool valid=true;
        for(unsigned i=0;i<count;++i) {
            float expected=simulation.height(int(i)/16,int(i)%16);
            if(!std::isfinite(values[i]) || std::abs(values[i]-expected)>2e-5f) valid=false;
            unsigned age=i/16,band=i%16,physical=(simulation.newest()+simulation.rows()-age)%simulation.rows();
            float color=std::pow(std::abs(simulation.row(physical)[band].colorHeight),std::log(.5f)/std::log(settings.colorMidpoint));
            if(!std::isfinite(values[count+i]) || std::abs(values[count+i]-color)>2e-5f) valid=false;
        }
        context_->Unmap(staging.Get(),0);
        if(!valid) throw std::runtime_error("Gravity GPU nie odpowiada modelowi CPU");
    };
    compare(1,9.81f); compare(1.25,9.81f); // Upward launch, independent heights and trajectories.
    compare(1.25,30); compare(1.35,30);   // Raise gravity without jumping.
    compare(1.35,.1f); compare(1.8,.1f); // Lower gravity while retaining downward velocity.
    compare(4,.1f);                     // Landed points stay on the floor.
    simulation.resize(16,16,0); settings.maxHeight=15;
    compare(1,.1f); compare(8.5,.1f); compare(9,.1f); // Long-lived motion across clock rebase.
    simulation.resize(16,16,0); settings.maxHeight=4;
    simulation.resize(16,16,0); compare(1,9.81f); compare(3,9.81f); compare(5,9.81f);
    simulation.resize(16,16,0); compare(1,9.81f); compare(1,.1f); compare(2,.1f); // Lower gravity during ascent: no artificial height cap.
}
std::string Renderer::adapterName() const {
    ComPtr<IDXGIDevice> dxgi; check(device_.As(&dxgi),"GPU info"); ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"GPU adapter");
    DXGI_ADAPTER_DESC d{}; check(adapter->GetDesc(&d),"GPU description"); return utf8(d.Description);
}
PixelStats Renderer::pixelStats() {
    ComPtr<ID3D11Texture2D> buffer,copy; check(swap_->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Pixel stats buffer");
    D3D11_TEXTURE2D_DESC d; buffer->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device_->CreateTexture2D(&d,nullptr,&copy),"Pixel stats texture"); context_->CopyResource(copy.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE map; check(context_->Map(copy.Get(),0,D3D11_MAP_READ,0,&map),"Pixel stats map");
    PixelStats stats;
    for(unsigned y=0;y<height_;++y) for(unsigned x=0;x<width_;++x) {
        auto p=static_cast<const BYTE*>(map.pData)+y*map.RowPitch+x*4;
        if(p[0] || p[1] || p[2]) ++stats.litPixels;
        if(p[2]>=254 && p[1]>=254 && p[0]<=1) ++stats.fullYellowPixels;
        if(p[0]>p[3]+1 || p[1]>p[3]+1 || p[2]>p[3]+1) ++stats.premultipliedViolations;
        stats.maxRed=std::max(stats.maxRed,p[2]/255.0f); stats.maxAlpha=std::max(stats.maxAlpha,p[3]/255.0f);
    }
    context_->Unmap(copy.Get(),0); return stats;
}
float Renderer::backgroundAlpha() {
    ComPtr<ID3D11Texture2D> buffer,copy; check(swap_->GetBuffer(0,IID_PPV_ARGS(&buffer)),"GetBuffer");
    D3D11_TEXTURE2D_DESC d{}; buffer->GetDesc(&d); d.Width=d.Height=1; d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device_->CreateTexture2D(&d,nullptr,&copy),"Readback"); D3D11_BOX box{0,0,0,1,1,1}; context_->CopySubresourceRegion(copy.Get(),0,0,0,0,buffer.Get(),0,&box);
    D3D11_MAPPED_SUBRESOURCE map; check(context_->Map(copy.Get(),0,D3D11_MAP_READ,0,&map),"Readback Map"); float a=static_cast<const BYTE*>(map.pData)[3]/255.0f; context_->Unmap(copy.Get(),0); return a;
}
void Renderer::screenshot(const std::filesystem::path& path) {
    ComPtr<ID3D11Texture2D> buffer,copy; check(swap_->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Screenshot buffer");
    D3D11_TEXTURE2D_DESC d; buffer->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device_->CreateTexture2D(&d,nullptr,&copy),"Screenshot texture"); context_->CopyResource(copy.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE map; check(context_->Map(copy.Get(),0,D3D11_MAP_READ,0,&map),"Screenshot Map");
    BITMAPFILEHEADER file{}; file.bfType=0x4D42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+width_*height_*4;
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=LONG(width_); info.biHeight=-LONG(height_); info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
    std::ofstream stream(path,std::ios::binary); stream.write(reinterpret_cast<const char*>(&file),sizeof(file)); stream.write(reinterpret_cast<const char*>(&info),sizeof(info));
    for(unsigned y=0;y<height_;++y) stream.write(static_cast<const char*>(map.pData)+y*map.RowPitch,width_*4);
    context_->Unmap(copy.Get(),0); if(!stream) throw std::runtime_error("Failed to save screenshot");
}
}
