/**
 * Constructs a renderer object.
 * @param {WebGLRenderingContext} gl The GL context.
 * @constructor
 */
var Renderer = function (gl) {
   /**
    * The GL context.
    * @type {WebGLRenderingContext}
    * @private
    */
   this.gl_ = gl;

   /**
    * A vertex buffer containing a single quad with xy coordinates from [-1,-1]
    * to [1,1] and uv coordinates from [0,0] to [1,1].
    * @private
    */
   this.quadVertexBuffer_ = gl.createBuffer();
   gl.bindBuffer(gl.ARRAY_BUFFER, this.quadVertexBuffer_);

   var vertices = new Float32Array(
      [-1.0, -1.0, 0.0, 1.0,
      +1.0, -1.0, 1.0, 1.0,
      -1.0, +1.0, 0.0, 0.0,
         1.0, +1.0, 1.0, 0.0]);
		 
//   var vertices = new Float32Array(
//      [-1.0, -1.0, 0.0, .5,
//      +1.0, -1.0, .5, .5,
//      -1.0, +1.0, 0.0, 0.0,
//         1.0, +1.0, .5, 0.0]);
		 
   gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);

   /**
    * The default display program (no deblocking).
    * @private
    */
   this.program_ = this.buildProgram_(
      Renderer.vertexShaderSource_,
      Renderer.fragmentShaderSource_);

   /**
    * The deblocking program.
    * @private
    */
   this.deblockProgram_ = this.buildProgram_(
      Renderer.vertexShaderSource_,
      Renderer.deblockFragmentShaderSource_);

   // Bind the default program so any code relying on state after construction
   // sees the same program active as before.
   gl.useProgram(this.program_.program);
   gl.enableVertexAttribArray(0);

   gl.enable(gl.DEPTH_TEST);
   gl.disable(gl.CULL_FACE);
};


/**
 * Compiles, links, and introspects a program. Returns an object bundling the
 * program handle with its cached uniform and attribute locations.
 * @param {string} vertexSource
 * @param {string} fragmentSource
 * @return {{program: WebGLProgram, uniforms: !Object, attribs: !Object}}
 * @private
 */
Renderer.prototype.buildProgram_ = function (vertexSource, fragmentSource) {
   var gl = this.gl_;
   var program = gl.createProgram();
   var vs = this.compileShader_(vertexSource, gl.VERTEX_SHADER);
   var fs = this.compileShader_(fragmentSource, gl.FRAGMENT_SHADER);

   gl.attachShader(program, vs);
   gl.attachShader(program, fs);
   gl.bindAttribLocation(program, 0, 'vert');
   gl.linkProgram(program);

   if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      var log = gl.getProgramInfoLog(program);
      console.error('Program link failed:\n' + log);
      throw new Error('Program link failed');
   }

   var uniforms = {};
   var uniformCount = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
   for (var i = 0; i < /** @type {number} */(uniformCount); i++) {
      var uinfo = gl.getActiveUniform(program, i);
      uniforms[uinfo.name] = gl.getUniformLocation(program, uinfo.name);
   }

   var attribs = {};
   var attribCount = gl.getProgramParameter(program, gl.ACTIVE_ATTRIBUTES);
   for (var j = 0; j < /** @type {number} */(attribCount); j++) {
      var ainfo = gl.getActiveAttrib(program, j);
      attribs[ainfo.name] = gl.getAttribLocation(program, ainfo.name);
   }

   return { program: program, uniforms: uniforms, attribs: attribs };
};


Renderer.prototype.finishInit = function () {
   //this.draw();
};


/**
 * Sets the color space the compositor uses to interpret backbuffer pixels.
 * Pure display-side knob: does not modify pixel values, just relabels them.
 * No-op (returns false) on browsers/contexts that don't support the property
 * or don't accept the requested value.
 * @param {string} colorSpace e.g. "srgb" or "display-p3"
 * @return {boolean} true if the requested color space is now active.
 */
Renderer.prototype.setCanvasColorSpace = function (colorSpace) {
   var gl = this.gl_;
   if (typeof gl.drawingBufferColorSpace === 'undefined') return false;
   try {
      gl.drawingBufferColorSpace = colorSpace;
   } catch (e) {
      return false;
   }
   return gl.drawingBufferColorSpace === colorSpace;
};
Renderer.prototype.createDxtTexture = function (dxtData, width, height, format) {
   var gl = this.gl_;
   var tex = gl.createTexture();
   gl.bindTexture(gl.TEXTURE_2D, tex);
   gl.compressedTexImage2D(
      gl.TEXTURE_2D,
      0,
      format,
      width,
      height,
      0,
      dxtData);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
   gl.bindTexture(gl.TEXTURE_2D, null);
   return tex;
};

Renderer.prototype.createCompressedTexture = function (data, width, height, format) {
   var gl = this.gl_;
   var tex = gl.createTexture();
   gl.bindTexture(gl.TEXTURE_2D, tex);
   gl.compressedTexImage2D(
      gl.TEXTURE_2D,
      0,
      format,
      width,
      height,
      0,
      data);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
   gl.bindTexture(gl.TEXTURE_2D, null);
   return tex;
};

Renderer.prototype.createHalfRGBATexture = function (data, width, height, format) {
   var gl = this.gl_;
   var tex = gl.createTexture();
   gl.bindTexture(gl.TEXTURE_2D, tex);
   gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      width,
      height,
      0,
      gl.RGBA,
      format,
      data);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
   gl.bindTexture(gl.TEXTURE_2D, null);
   return tex;
};

// WebGL requires each row of rgb565Data to be aligned on a 4-byte boundary.            
Renderer.prototype.createRgb565Texture = function (rgb565Data, width, height) {
   var gl = this.gl_;
   var tex = gl.createTexture();
   gl.bindTexture(gl.TEXTURE_2D, tex);
   
   gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGB,
      width,
      height,
      0,
      gl.RGB,
      gl.UNSIGNED_SHORT_5_6_5,
      rgb565Data);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
   gl.bindTexture(gl.TEXTURE_2D, null);
   return tex;
};

Renderer.prototype.createRgbaTexture = function (rgbaData, width, height) {
   var gl = this.gl_;
   var tex = gl.createTexture();
   gl.bindTexture(gl.TEXTURE_2D, tex);
   gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      width,
      height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      rgbaData);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
   gl.bindTexture(gl.TEXTURE_2D, null);
   return tex;
};


/**
 * Draws the given texture across the viewport.
 *
 * Signature is preserved exactly from the original, pre-deblocking Renderer.
 * Existing call sites do not need to change. For the deblocking variant, use
 * drawTextureDeblocked.
 *
 * @param {WebGLTexture} texture
 * @param {number} width
 * @param {number} height
 * @param {number} mode 0=normal, 1=RGB only, 2=alpha only, 3/4/5=R/G/B isolate
 * @param {number} scale Exposure multiplier applied to RGB.
 * @param {boolean} linearToSRGBFlag
 * @param {boolean} useLinearFiltering
 */
Renderer.prototype.drawTexture = function (texture, width, height, mode, scale, linearToSRGBFlag, useLinearFiltering) {
   var gl = this.gl_;
   var prog = this.program_;
   gl.useProgram(prog.program);

   // draw scene
   gl.clearColor(0, 0, 0, 1);
   gl.clearDepth(1.0);
   gl.viewport(0, 0, width, height);
   gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);

   gl.activeTexture(gl.TEXTURE0);
   gl.bindTexture(gl.TEXTURE_2D, texture);

   // Point vs. bilinear sampling (no mipmaps involved here)
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, useLinearFiltering ? gl.LINEAR : gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, useLinearFiltering ? gl.LINEAR : gl.NEAREST);

   gl.uniform1i(prog.uniforms.texSampler, 0);

   var x = 0.0;
   var y = 0.0;
   if (mode == 1)
      x = 1.0;
   else if (mode == 2)
      y = 1.0;

   gl.uniform4f(prog.uniforms.control, x, y, scale, linearToSRGBFlag ? 1.0 : 0.0);

   var a = 1.0 / width;
   var b = 1.0 / height;
   var c = (mode >= 3) ? (mode - 2) : 0;  // mode 3->1(R), 4->2(G), 5->3(B)
   var d = 0;
   gl.uniform4f(prog.uniforms.control2, a, b, c, d);

   gl.enableVertexAttribArray(prog.attribs.vert);
   gl.bindBuffer(gl.ARRAY_BUFFER, this.quadVertexBuffer_);
   gl.vertexAttribPointer(prog.attribs.vert, 4, gl.FLOAT,
      false, 0, 0);
   gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};


/**
 * Draws the given texture across the viewport with a block-edge deblocking
 * filter applied. Intended for previewing block-compressed textures (DXT, BC,
 * ASTC, etc.) where compression blocks can introduce visible seams.
 *
 * The post-processing knobs (mode, scale, linearToSRGBFlag, useLinearFiltering)
 * behave exactly as in drawTexture, and run on the deblocked RGBA.
 *
 * width/height are the texture's native dimensions (they double as viewport
 * size because this renderer always draws 1:1).
 *
 * @param {WebGLTexture} texture
 * @param {number} width
 * @param {number} height
 * @param {number} mode
 * @param {number} scale
 * @param {boolean} linearToSRGBFlag
 * @param {boolean} useLinearFiltering
 * @param {number} blockWidth Block width in texels (e.g. 4 for DXT/BC).
 * @param {number} blockHeight Block height in texels.
 * @param {boolean} showEdgeWeights Visualize the edge-weight mask instead of
 *     the deblocked image.
 */
Renderer.prototype.drawTextureDeblocked = function (texture, width, height, mode, scale, linearToSRGBFlag, useLinearFiltering, blockWidth, blockHeight, showEdgeWeights) {
   var gl = this.gl_;
   var prog = this.deblockProgram_;
   gl.useProgram(prog.program);

   gl.clearColor(0, 0, 0, 1);
   gl.clearDepth(1.0);
   gl.viewport(0, 0, width, height);
   gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);

   gl.activeTexture(gl.TEXTURE0);
   gl.bindTexture(gl.TEXTURE_2D, texture);

   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, useLinearFiltering ? gl.LINEAR : gl.NEAREST);
   gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, useLinearFiltering ? gl.LINEAR : gl.NEAREST);

   gl.uniform1i(prog.uniforms.texSampler, 0);

   var x = 0.0;
   var y = 0.0;
   if (mode == 1)
      x = 1.0;
   else if (mode == 2)
      y = 1.0;

   gl.uniform4f(prog.uniforms.control, x, y, scale, linearToSRGBFlag ? 1.0 : 0.0);

   var a = 1.0 / width;
   var b = 1.0 / height;
   var c = (mode >= 3) ? (mode - 2) : 0;
   var d = 0;
   gl.uniform4f(prog.uniforms.control2, a, b, c, d);

   // texSize: xy = texture dims, zw = block dims.
   gl.uniform4f(prog.uniforms.texSize, width, height, blockWidth, blockHeight);

   // const0.x: master deblock enable. const0.y: edge-weight visualization.
   gl.uniform4f(prog.uniforms.const0, 1.0, showEdgeWeights ? 1.0 : 0.0, 0.0, 0.0);

   gl.enableVertexAttribArray(prog.attribs.vert);
   gl.bindBuffer(gl.ARRAY_BUFFER, this.quadVertexBuffer_);
   gl.vertexAttribPointer(prog.attribs.vert, 4, gl.FLOAT,
      false, 0, 0);
   gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};


/**
 * Compiles a GLSL shader and returns a WebGLShader.
 * @param {string} shaderSource The shader source code string.
 * @param {number} type Either VERTEX_SHADER or FRAGMENT_SHADER.
 * @return {WebGLShader} The new WebGLShader.
 * @private
 */
Renderer.prototype.compileShader_ = function (shaderSource, type) {
   var gl = this.gl_;
   var shader = gl.createShader(type);
   gl.shaderSource(shader, shaderSource);
   gl.compileShader(shader);
   
     // Check for errors
   const compiled = gl.getShaderParameter(shader, gl.COMPILE_STATUS);
   if (!compiled) {
        const errorLog = gl.getShaderInfoLog(shader);
        console.error(`Error compiling ${type === gl.VERTEX_SHADER ? 'vertex' : 'fragment'} shader:\n${errorLog}`);
        gl.deleteShader(shader); // Cleanup shader object
        throw new Error('Shader compilation failed');
   }
    
   return shader;
};


/**
 * @type {string}
 * @private
 */
Renderer.vertexShaderSource_ = /* glsl */ `
   attribute vec4 vert;
   varying vec2 v_texCoord;

   void main() {
      gl_Position = vec4(vert.xy, 0.0, 1.0);
      v_texCoord = vert.zw;
   }
`;


/**
 * @type {string}
 * @private
 */
Renderer.fragmentShaderSource_ = /* glsl */ `
   precision highp float;

   uniform sampler2D texSampler;
   uniform vec4 control;
   uniform vec4 control2;
   varying vec2 v_texCoord;

   vec3 linearToSrgb(vec3 linearRGB) {
      vec3 srgbLow  = linearRGB * 12.92;
      vec3 srgbHigh = 1.055 * pow(linearRGB, vec3(1.0 / 2.4)) - 0.055;
      return clamp(mix(srgbLow, srgbHigh, step(0.0031308, linearRGB)), 0.0, 1.0);
   }

   void main() {
      vec4 c = texture2D(texSampler, v_texCoord);
      c.rgb *= control.z;

      if (control.x > 0.0) {
         c.w = 1.0;
      } else if (control.y > 0.0) {
         c.rgb = c.aaa;
         c.w = 1.0;
      }

      if      (control2.z == 1.0) { c.rgb = vec3(c.r); c.w = 1.0; }
      else if (control2.z == 2.0) { c.rgb = vec3(c.g); c.w = 1.0; }
      else if (control2.z == 3.0) { c.rgb = vec3(c.b); c.w = 1.0; }

      if (control.w > 0.0)
         c.rgb = linearToSrgb(c.rgb);

      gl_FragColor = c;
   }
`;


/**
 * Deblocking fragment shader. Structurally identical to fragmentShaderSource_
 * except the single texture2D(...) fetch at the top is replaced by a block-
 * aware 3-tap separable filter that softens texels near block boundaries. The
 * existing post-process chain (exposure, alpha-only, channel isolate,
 * linear->sRGB) then runs on the deblocked RGBA unchanged.
 *
 * Uniforms:
 *   texSize  xy = source texture dimensions in texels,
 *            zw = block dimensions in texels (e.g. 4x4 for DXT/BC7, ASTC sizes
 *                 vary)
 *   const0   x  = master deblock enable (>0.5),
 *            y  = edge-weight visualization (>0.5)
 *
 * This shader assumes the texture is drawn 2D with no mipmap chain
 * (mipScale = 1). If you later add mipmapping, restore the dFdx/dFdy-based
 * mipScale derivation and add the OES_standard_derivatives extension.
 * @type {string}
 * @private
 */
Renderer.deblockFragmentShaderSource_ = /* glsl */ `
   precision highp float;

   uniform sampler2D texSampler;
   uniform vec4 control;
   uniform vec4 control2;
   uniform vec4 texSize;  // xy = tex dims, zw = block dims
   uniform vec4 const0;   // x = deblock on, y = show edge weights
   varying vec2 v_texCoord;

   vec3 linearToSrgb(vec3 linearRGB) {
      vec3 srgbLow  = linearRGB * 12.92;
      vec3 srgbHigh = 1.055 * pow(linearRGB, vec3(1.0 / 2.4)) - 0.055;
      return clamp(mix(srgbLow, srgbHigh, step(0.0031308, linearRGB)), 0.0, 1.0);
   }

   void main() {
      vec2 texDim    = texSize.xy;
      vec2 blockSize = texSize.zw;

      vec2 texelStep = 1.0 / texDim;             // one texel in mip0 space
      vec2 texelPos  = (v_texCoord * texDim);    // texel coord in mip0 space
      vec2 blockPos  = mod(texelPos, blockSize); // texel position within the current block

      vec4 c = texture2D(texSampler, v_texCoord);
	  
	  vec4 l1 = texture2D(texSampler, v_texCoord - vec2(texelStep.x, 0.0));
      vec4 r1 = texture2D(texSampler, v_texCoord + vec2(texelStep.x, 0.0));
      vec4 u1 = texture2D(texSampler, v_texCoord - vec2(0.0, texelStep.y));
      vec4 d1 = texture2D(texSampler, v_texCoord + vec2(0.0, texelStep.y));
      	  
      if (const0.x > 0.5) {
	     // Note this shader assumes a quad will be rendered to a viewport the same size as the texture, with point/nearest sampling. If you're going to render with bilinear filtering or mipmapping, this shader isn't the right one.
         const float falloff = 1.0;

         float leftProx   = 1.0 - clamp(blockPos.x / falloff, 0.0, 1.0);
         float rightProx  = 1.0 - clamp((blockSize.x - blockPos.x) / falloff, 0.0, 1.0);
         float topProx    = 1.0 - clamp(blockPos.y / falloff, 0.0, 1.0);
         float bottomProx = 1.0 - clamp((blockSize.y - blockPos.y) / falloff, 0.0, 1.0);

         // Scale weights by 2.0 to ensure the inner ring of samples have 1.0 edge weights (if we knew bilinear sampling was going to be used, this would be 1.0).
		 const float K = 2.0;
		 float horizWeight = min(1.0, K*max(leftProx, rightProx));
         float vertWeight  = min(1.0, K*max(topProx, bottomProx));
         float edgeWeight  = max(horizWeight, vertWeight);

         if (edgeWeight > 0.0) {
            vec4 c0 = c;
                        
            vec4 filteredH = (l1 + c0 + r1) * (1.0 / 3.0);
            vec4 filteredV = (u1 + c0 + d1) * (1.0 / 3.0);

            float strengthH = horizWeight;
            float strengthV = vertWeight;
            vec4 horizColor = mix(c0, filteredH, strengthH);
            vec4 vertColor  = mix(c0, filteredV, strengthV);

            float totalW = strengthH + strengthV;
            if (totalW > 0.0)
               c = (horizColor * strengthH + vertColor * strengthV) / totalW;
         }

         // Block edge visualization.
         if (const0.y > 0.5) {
            c = vec4(edgeWeight, edgeWeight, edgeWeight, 1.0);
         }
      }

      // --- Post-process chain (identical to fragmentShaderSource_) ---
      c.rgb *= control.z;

      if (control.x > 0.0) {
         c.w = 1.0;
      } else if (control.y > 0.0) {
         c.rgb = c.aaa;
         c.w = 1.0;
      }

      if      (control2.z == 1.0) { c.rgb = vec3(c.r); c.w = 1.0; }
      else if (control2.z == 2.0) { c.rgb = vec3(c.g); c.w = 1.0; }
      else if (control2.z == 3.0) { c.rgb = vec3(c.b); c.w = 1.0; }

      if (control.w > 0.0)
         c.rgb = linearToSrgb(c.rgb);

      gl_FragColor = c;
   }
`;
