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
uniform vec4 texSize;  // Base mip dimensions (mip 0)
uniform vec4 const0;   // User constant 0 (keys 1-4 toggle x,y,z,w)
uniform vec4 const1;   // User constant 1 (keys 5-8 toggle x,y,z,w)

in vec2 vUV;
out vec4 fragColor;

void main() 
{
    vec2 blockSize = vec2(texSize.z, texSize.w);
    
    vec2 du = dFdx(vUV);
    vec2 dv = dFdy(vUV);
    float rho = max(length(du * texSize.xy), length(dv * texSize.xy));
    float lod = max(0.0, log2(max(rho, 1e-8))); // lod index
    float mipScale = exp2(floor(lod + .5)); // 2^lod mipmap scale, snaps to dominant mipmap
    
    vec2 texDim = vec2(texSize.x, texSize.y);
    vec2 texelStep = 1.0 / texDim;
    vec2 texelPos = (vUV * texDim) / mipScale;
    vec2 blockPos = mod(texelPos, blockSize);
    
    vec3 color;
    color = texture(tex, vUV).rgb;
    
    if (const0.x > 0.5) 
    {
        float falloff = 2.0;
        
        float leftProx = 1.0 - clamp(blockPos.x / falloff, 0.0, 1.0);
        float rightProx = 1.0 - clamp((blockSize.x - 1.0 - blockPos.x) / falloff, 0.0, 1.0);
        float topProx = 1.0 - clamp(blockPos.y / falloff, 0.0, 1.0);
        float bottomProx = 1.0 - clamp((blockSize.y - 1.0 - blockPos.y) / falloff, 0.0, 1.0);

        float horizWeight = max(leftProx, rightProx);
        float vertWeight = max(topProx, bottomProx);
        float edgeWeight = max(horizWeight, vertWeight); // overall proximity
                
        vec3 c0 = color; //texture2D(tex, vUV).rgb;

        vec3 l2 = texture2D(tex, vUV - vec2(2 * texelStep.x * mipScale, 0.0)).rgb;      
        vec3 l1 = texture2D(tex, vUV - vec2(texelStep.x * mipScale, 0.0)).rgb;
        vec3 r1 = texture2D(tex, vUV + vec2(texelStep.x * mipScale, 0.0)).rgb;
        vec3 r2 = texture2D(tex, vUV + vec2(2 * texelStep.x * mipScale, 0.0)).rgb;
        
        vec3 u2 = texture2D(tex, vUV - vec2(0.0, 2  * texelStep.y * mipScale)).rgb;
        vec3 u1 = texture2D(tex, vUV - vec2(0.0, texelStep.y * mipScale)).rgb;
        vec3 d1 = texture2D(tex, vUV + vec2(0.0, texelStep.y * mipScale)).rgb;
        vec3 d2 = texture2D(tex, vUV + vec2(0.0, 2 * texelStep.y * mipScale)).rgb;
        
        //vec3 filteredH = (l2 + 2 * l1 + 3 * c0 + 2 * r1 + r2) / 9.0;
        //vec3 filteredV = (u2 + 2 * u1 + 3 * c0 + 2 * d1 + d2) / 9.0;
		
		vec3 filteredH = (l2 + 2 * l1 + 2 * c0 + 2 * r1 + r2) / 8.0;
        vec3 filteredV = (u2 + 2 * u1 + 2 * c0 + 2 * d1 + d2) / 8.0;
        
        float smoothH = 1.0;
        float smoothV = 1.0;
        
        if (edgeWeight > 0.0) 
        {
            vec3 horizColor = mix(c0, filteredH, smoothH * horizWeight);
            vec3 vertColor = mix(c0, filteredV, smoothV * vertWeight);
            
            float totalW = horizWeight + vertWeight;
            if (totalW > 0.0)
                color = (horizColor * horizWeight + vertColor * vertWeight) / totalW;
        }

		// block edge vis
        if (const0.y > 0.5)         
        {
            color = vec3(edgeWeight, edgeWeight, edgeWeight);
        }
    }
            
    fragColor = vec4(color, 1.0);
}
