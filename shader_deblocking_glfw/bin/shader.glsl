#vertex
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 mvp;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = mvp * vec4(aPos, 1.0);
}

#fragment
#version 330 core

uniform sampler2D tex;
uniform vec4 texSize;  // Base mip dimensions (mip 0) in xy, zw=ASTC block size of texture
uniform float maxLod;  // = number_of_mip_levels - 1

uniform vec4 const0;   // User constant 0 (keys 1-4 toggle x,y,z,w)
uniform vec4 const1;   // User constant 1 (keys 5-8 toggle x,y,z,w)

in vec2 vUV;
out vec4 fragColor;

void main() 
{
    // Note: Could use textureQueryLod() but it's not supported in WebGL. textureSize() is in WebGL 2.0 however.
    const vec3 LUMA = vec3(0.299, 0.587, 0.114);

    vec2 texDim = vec2(texSize.x, texSize.y);
    vec2 blockSize = vec2(texSize.z, texSize.w);
    
    vec2 du = dFdx(vUV);
    vec2 dv = dFdy(vUV);
    float rho = max(length(du * texSize.xy), length(dv * texSize.xy));
    float lod = clamp(log2(max(rho, 1e-8)), 0.0, maxLod); // lod index
    float mipScale = exp2(floor(lod + .5)); // 2^lod mipmap scale, snaps to dominant mipmap, 1=mip0, 2=mip1, 4=mip2, etc.
        
    vec2 texelStep = mipScale / texDim; // how to step one texel in effective mip space
    vec2 texelPos = (vUV * texDim) / mipScale; // the physical texel coord in effective mip space
    vec2 blockPos = mod(texelPos, blockSize); // the block offset in texels
            
    vec3 color;
    color = texture(tex, vUV).rgb;

    // Keep these fetches outside non-uniform control flow: texture() uses implicit
    // derivatives for LOD, which are undefined when neighboring fragments take
    // different branches. Use explicit LOD so edgeWeight can safely go to zero.
    vec3 l1 = texture(tex, vUV - vec2(texelStep.x, 0.0)).rgb;
    vec3 r1 = texture(tex, vUV + vec2(texelStep.x, 0.0)).rgb;
                    
    vec3 u1 = texture(tex, vUV - vec2(0.0, texelStep.y)).rgb;
    vec3 d1 = texture(tex, vUV + vec2(0.0, texelStep.y)).rgb;

    if (const0.x > 0.5) 
    {
        const float falloff = 1.5;
        
        float leftProx   = 1.0 - clamp(blockPos.x / falloff, 0.0, 1.0);
        float rightProx  = 1.0 - clamp((blockSize.x - blockPos.x) / falloff, 0.0, 1.0);
        float topProx    = 1.0 - clamp(blockPos.y / falloff, 0.0, 1.0);
        float bottomProx = 1.0 - clamp((blockSize.y - blockPos.y) / falloff, 0.0, 1.0);

        float horizWeight = max(leftProx, rightProx);
        float vertWeight = max(topProx, bottomProx);
        float edgeWeight = max(horizWeight, vertWeight); // overall proximity
        
        if (edgeWeight > 0.0)
        {                       
            vec3 c0 = color;
                                    
            vec3 filteredH = (l1 + c0 + r1) * (1.0/3.0);
            vec3 filteredV = (u1 + c0 + d1) * (1.0/3.0);
        
            float strengthH = horizWeight;
            float strengthV = vertWeight;

            vec3 horizColor = mix(c0, filteredH, strengthH);
            vec3 vertColor  = mix(c0, filteredV, strengthV);

            float totalW = strengthH + strengthV;
            if (totalW > 0.0)
                color = (horizColor * strengthH + vertColor * strengthV) / totalW;
        }

        // block edge vis
        if (const0.y > 0.5)         
        {
            color = vec3(edgeWeight, edgeWeight, edgeWeight);
        }
    }
            
    fragColor = vec4(color, 1.0);
}
