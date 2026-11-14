#include "shaders/XrHud.hlsli"
[[vk::push_constant]] ConstantBuffer<RasterParams> Params;
[[vk::binding(0,0)]] RWStructuredBuffer<float4> Results;
[numthreads(1,1,1)]
void CSMain() {
    Results[Params.renderIndex] = xrHudPosition(float4(0,0,.2f,1), Params);
    Results[Params.renderIndex+1] = xrHudPosition(float4(1,1,.5f,1), Params);
    Results[Params.renderIndex+2] = xrHudPosition(float4(0,0,.8f,1), Params);
    // A compressed/rotated panel no longer shares the original N64 pixel grid.
    Results[Params.renderIndex+3] = float4(rasterTextureUV(float2(.2f,.3f), float2(.16f,.02f),
        float2(.03f,.18f),float2(12.5f,11.5f),float2(13.5f,19.8f),0,true),0,0);
    Results[Params.renderIndex+4] = float4(rasterTextureUV(float2(.2f,.3f), float2(.16f,.02f),
        float2(.03f,.18f),float2(12.5f,11.5f),float2(13.5f,19.8f),0,false),0,0);

}
