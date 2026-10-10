cbuffer Scene : register(b0) {
    row_major float4x4 viewProjection;
    float4 shape; // spacing, max height, point radius, local simulation time
    float4 grid; // bands, columns, newest physical row, fractional scroll
    float4 colorLow;
    float4 colorHigh;
    float4 appearance; // brightness, glow strength, projection x, projection y
    float4 physics; // normalized gravity, color curve exponent, reserved
};
Texture2D<float4> history : register(t0);
float fallingHeight(float4 state) {
    float fallTime=max(0,shape.w-state.z);
    float h=state.x-state.y*fallTime-.5*physics.x*fallTime*fallTime;
    if(physics.x>0 && state.w>0 && physics.z>0) {
        float landing=(-state.y+sqrt(state.y*state.y+2*physics.x*state.x))/physics.x;
        if(fallTime>=landing) {
            float elapsed=fallTime-landing,up=sqrt(2*physics.x*physics.z*state.w);
            h=up*elapsed-.5*physics.x*elapsed*elapsed;
        }
    }
    return max(0,h);
}
struct VertexOut { float4 position:SV_POSITION; float2 uv:TEXCOORD0; float level:TEXCOORD1; float opacity:TEXCOORD2; };
float paletteLevel(float height) {
    // Exponent is log(0.5)/log(midpoint), computed once per frame on CPU.
    // Exactly half the color transition occurs at the selected height.
    return pow(saturate(height),physics.y);
}
VertexOut VSMain(uint vertex:SV_VertexID,uint instance:SV_InstanceID) {
    const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
    uint bands=(uint)grid.x, columns=(uint)grid.y, age=instance/bands, band=instance%bands;
    uint physical=((uint)grid.z+columns+1-age)%(columns+1);
    float4 state=history.Load(int3(band,physical,0));
    float level=fallingHeight(state);
    float x=(columns-1)*.5-age-grid.w;
    float3 p=float3(x*shape.x,level*shape.y,(band-(bands-1)*.5)*shape.x);
    VertexOut o; o.position=mul(float4(p,1),viewProjection);
    float radius=shape.z*(appearance.y>0?2.4:1.0);
    o.position.xy+=corners[vertex]*radius*appearance.zw;
    o.uv=corners[vertex]; o.level=paletteLevel(abs(state.w)); // Original target, independent of current flight height.
    o.opacity=saturate((x+(columns-1)*.5)+1);
    if(age==0) o.opacity*=smoothstep(0,.18,grid.w);
    return o;
}
float4 PSMain(VertexOut i):SV_TARGET {
    float r=length(i.uv), scale=appearance.y>0?2.4:1;
    float core=1-smoothstep(.55,.98,r*scale);
    float halo=appearance.y*exp(-4*r*r)*(1-smoothstep(.8,1,r));
    float alpha=saturate((core+halo)*appearance.x)*i.opacity;
    clip(alpha-.001);
    float3 color=lerp(colorLow.rgb,colorHigh.rgb,i.level);
    color*=appearance.x;
    // Preserve channel proportions at high brightness instead of clipping
    // red and green separately into premature yellow.
    color/=max(1,max(color.r,max(color.g,color.b)));
    return float4(color*alpha,alpha); // Premultiplied alpha; background has its own opacity.
}

// Compiled and dispatched only by --smoke-test. Exercises the same GPU function,
// texture layout and constants as VSMain, with deterministic nonzero heights.
RWStructuredBuffer<float> validationHeights : register(u0);
[numthreads(64,1,1)]
void CSValidateGravity(uint3 id:SV_DispatchThreadID) {
    uint bands=(uint)grid.x,rows=(uint)grid.y+1;
    if(id.x>=bands*rows) return;
    uint age=id.x/bands,band=id.x%bands;
    uint physical=((uint)grid.z+rows-age)%rows;
    float4 state=history.Load(int3(band,physical,0));
    validationHeights[id.x]=fallingHeight(state);
    validationHeights[bands*rows+id.x]=paletteLevel(abs(state.w));
}
