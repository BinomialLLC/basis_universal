// Mipmap-Compatible Texture Sampling Deblocking Shader - D3D11 / HLSL (SM 5.0) port.
// Copyright (C) 2026 Binomial LLC.  LICENSE: Apache 2.0
//
// Direct port of shader_deblocking_glfw/bin/shader.glsl (the reference GLSL version).
// The deblocking operator lives in DeblockSample() so engine integrators can lift it
// verbatim; the VS/PS entry points below exist only for the host sample.
//
// This is an IN-LOOP filter: the Basis Universal encoder factors this exact operator
// into its rate/quality decisions. It is the decoder half of the codec, not an optional
// post-process. When a KTX2 file's DeblockingFilterIndex == 1, viewers should enable it
// by default -- and the CPU transcoder must be given cDecodeFlagsNoDeblockFiltering so
// the image isn't deblocked twice.
//
// blockSize is a property of the encoded SOURCE, not the GPU format: an 8x8 XUASTC
// source transcoded to BC7 still carries seams on the 8x8 source grid. Always pass the
// block size from the KTX2 header (get_block_width()/get_block_height()).

cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;       // CPU builds row-major and transposes once at cbuffer-write time
                        // (the standard idiom); this matrix is column-major here, so
                        // mul(mvp, v) below matches the GL sample's "mvp * vec4(...)".
    float4   texSize;   // xy = dims of the texture as created (mip 0), zw = SOURCE block size in texels
    float4   lodInfo;   // x = maxLod (= number_of_mip_levels - 1); yzw = padding (16-byte cbuffer rules)
    float4   const0;    // x = deblock on/off, y = edge-weight visualization (keys 1-4 toggle x,y,z,w)
    float4   const1;    // spare user constants (keys 5-8 toggle x,y,z,w)
};

Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

// ---------------------------------------------------------------------------
// The deblocking operator. 5-tap cross, 3-tap box averages, edge-proximity blend
// with corner normalization. Reusable: this function + a Texture2D/SamplerState
// pair is everything an engine needs.
//
// falloff is the edge-proximity radius in texels. It is a tolerance, not a codec
// constant: 1.0 suits flat 2D viewers, 1.5 is more stable under minification and
// oblique viewing angles in 3D (this sample uses 1.5, matching the GL/WebGL ports).
//
// This variant also returns the block-edge weight for visualization; the wrapper
// below has the plain signature.
// ---------------------------------------------------------------------------
float3 DeblockSampleEx(Texture2D tex, SamplerState samp, float2 uv,
                       float2 texDim, float2 blockSize, float maxLod,
                       float falloff, out float outEdgeWeight)
{
    // Note: Could use tex.CalculateLevelOfDetail(samp, uv) (SM 4.1+) instead of the
    // derivative math below, but the manual path is the portable reference and matches
    // the GL/WebGL versions line-for-line.
    //
    // Recover the effective mip level from screen-space UV derivatives, so the
    // deblock lattice tracks whichever mip the hardware is actually sampling.
    float2 du = ddx(uv);
    float2 dv = ddy(uv);
    float rho = max(length(du * texDim), length(dv * texDim));
    float lod = clamp(log2(max(rho, 1e-8)), 0.0, maxLod); // lod index
    float mipScale = exp2(floor(lod + 0.5)); // 2^lod mipmap scale, snaps to dominant mipmap, 1=mip0, 2=mip1, 4=mip2, etc.

    float2 texelStep = mipScale / texDim;         // how to step one texel in effective mip space
    float2 texelPos  = (uv * texDim) / mipScale;  // the physical texel coord in effective mip space
    // fmod is safe here ONLY because texelPos is always non-negative (fmod and GLSL's
    // mod() differ on negative operands) -- do not "fix" this.
    float2 blockPos  = fmod(texelPos, blockSize); // the block offset in texels

    float3 color = tex.Sample(samp, uv).rgb;

    // Keep these fetches outside non-uniform control flow: Sample() uses implicit
    // derivatives for LOD, which are undefined when neighboring fragments take
    // different branches (HLSL compilers may silently emit them anyway). Everything
    // below the taps is pure arithmetic, so the divergent branch further down is safe.
    float3 l1 = tex.Sample(samp, uv - float2(texelStep.x, 0.0)).rgb;
    float3 r1 = tex.Sample(samp, uv + float2(texelStep.x, 0.0)).rgb;

    float3 u1 = tex.Sample(samp, uv - float2(0.0, texelStep.y)).rgb;
    float3 d1 = tex.Sample(samp, uv + float2(0.0, texelStep.y)).rgb;

    float leftProx   = 1.0 - clamp(blockPos.x / falloff, 0.0, 1.0);
    float rightProx  = 1.0 - clamp((blockSize.x - blockPos.x) / falloff, 0.0, 1.0);
    float topProx    = 1.0 - clamp(blockPos.y / falloff, 0.0, 1.0);
    float bottomProx = 1.0 - clamp((blockSize.y - blockPos.y) / falloff, 0.0, 1.0);

    float horizWeight = max(leftProx, rightProx);
    float vertWeight  = max(topProx, bottomProx);
    float edgeWeight  = max(horizWeight, vertWeight); // overall proximity
    outEdgeWeight = edgeWeight;

    if (edgeWeight > 0.0)
    {
        float3 c0 = color;

        float3 filteredH = (l1 + c0 + r1) * (1.0 / 3.0);
        float3 filteredV = (u1 + c0 + d1) * (1.0 / 3.0);

        float strengthH = horizWeight;
        float strengthV = vertWeight;

        float3 horizColor = lerp(c0, filteredH, strengthH);
        float3 vertColor  = lerp(c0, filteredV, strengthV);

        float totalW = strengthH + strengthV;
        if (totalW > 0.0)
            color = (horizColor * strengthH + vertColor * strengthV) / totalW;
    }

    return color;
}

// Plain signature for lifting into engines (no visualization output).
float3 DeblockSample(Texture2D tex, SamplerState samp, float2 uv,
                     float2 texDim, float2 blockSize, float maxLod,
                     float falloff /* codec tolerance: 1.5 for 3D, 1.0 for 2D */)
{
    float unusedEdgeWeight;
    return DeblockSampleEx(tex, samp, uv, texDim, blockSize, maxLod, falloff, unusedEdgeWeight);
}

// ---------------------------------------------------------------------------
// Host sample entry points.
// ---------------------------------------------------------------------------
struct VSInput
{
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.pos = mul(mvp, float4(input.pos, 1.0));
    o.uv  = input.uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 texDim    = texSize.xy;
    float2 blockSize = texSize.zw;
    const float falloff = 1.5;   // 3D tolerance; 2D viewers use 1.0 (tunable)

    // The deblocked result is computed unconditionally and selected by the uniform
    // toggle, so no gradient instruction (ddx/ddy or implicit-derivative Sample)
    // ever sits inside flow control -- the compiler cannot warn, and the taps
    // cannot go undefined.
    float  edgeWeight;
    float3 deblocked = DeblockSampleEx(tex, samp, input.uv, texDim, blockSize,
                                       lodInfo.x, falloff, edgeWeight);
    float3 plain = tex.Sample(samp, input.uv).rgb; // dedupes with DeblockSampleEx's center tap

    float3 color = (const0.x > 0.5) ? deblocked : plain;

    // block edge vis (key 2)
    if (const0.x > 0.5 && const0.y > 0.5)
        color = float3(edgeWeight, edgeWeight, edgeWeight);

    return float4(color, 1.0);
}
