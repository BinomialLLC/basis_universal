// encode_dto_apply.js
//
// applyEncodeDTO(encoder, dto): applies an encode DTO (built by buildEncodeDTO in
// index.html) to a BasisEncoder instance, in the SAME ORDER as the legacy inline
// encode path; the caller then calls encoder.encode().
//
// PURE: depends only on (encoder, dto) -- no Module, no DOM. That's why it can run
// unchanged on the main thread (non-worker DTO path) AND inside the encode worker
// (which importScripts() this file). All enum values in dto are already ints.
//
// Loaded on the page via <script src> and in the worker via importScripts(), so it
// is the single source of truth for both modes (no duplication / drift).

function applyEncodeDTO(encoder, dto)
{
   // container / threading
   encoder.controlThreading(dto.multithreaded, dto.numWorkerThreads);
   encoder.setCreateKTX2File(dto.createKTX2);
   encoder.setKTX2UASTCSupercompression(dto.ktx2UASTCSupercompression);

   // color / transfer
   encoder.setPerceptual(dto.sRGB);
   if (!dto.isHDRSourceFile)
      encoder.setKTX2AndBasisSRGBTransferFunc(dto.sRGB);
   encoder.setMipSRGB(dto.sRGB);

   // source image (the DTO normalized the 3 legacy cases into one shape:
   // pre-decoded RGBA has real dims; raw file bytes use 0,0 + an img_type)
   const s = dto.source;
   if (s.isHDRTarget)
      encoder.setSliceSourceImageHDR(0, s.bytes, s.width, s.height, s.imgType, s.convertLDRToLinear, s.nitMultiplier);
   else
      encoder.setSliceSourceImage(0, s.bytes, s.width, s.height, s.imgType);

   encoder.setFormatMode(dto.formatMode);
   encoder.setRec2020(dto.rec2020);
   encoder.setDebug(dto.debug);
   encoder.setComputeStats(dto.computeStats);
   encoder.setPrintStats(dto.printStats);
   encoder.setStatusOutput(true);

   // low-level codec opts -- run when the unified checkbox is OFF (legacy).
   // NOTE this can run even for XUBC7 (when checkbox is off); applyUnified then
   // overrides quality/effort afterward, exactly as the legacy path does.
   if (dto.lowLevelOptsEnabled)
   {
      encoder.setUASTCHDRQualityLevel(dto.uastcHDRQuality);
      encoder.setASTC_HDR_6x6_Level(dto.astcHDR6x6Level);
      encoder.setLambda(dto.astc6x6Lambda);

      // three-way quality branch (mirrors legacy): XUBC7 defers to the unified
      // call (no setQualityLevel here); XUASTC uses DCT; everything else ETC1S.
      if (dto.isXUBC7Target)
      {
         // intentionally nothing -- applyUnified handles XUBC7 quality/effort
      }
      else if (dto.isXUASTCLDRTarget)
      {
         if (dto.xuastcDCTQuality < 100)
         {
            encoder.setQualityLevel(dto.xuastcDCTQuality);
            encoder.setXUASTCLDRUseDCT(true);
         }
      }
      else
      {
         encoder.setQualityLevel(dto.etc1sQuality);
      }

      encoder.setXUASTCLDRUseLossySupercompression(dto.xuastcLossySupercompression);
      encoder.setASTCOrXUASTCLDREffortLevel(dto.astcXuastcEffortLevel);
      encoder.setRDOUASTC(dto.rdoUASTC);
      encoder.setRDOUASTCQualityScalar(dto.rdoUASTCQualityScalar);
      encoder.setPackUASTCFlags(dto.packUASTCFlags);
      encoder.setETC1SCompressionLevel(dto.etc1sCompLevel);
   }

   // always-applied opts (available even when the unified path is used)
   for (let i = 0; i < 6; i++)
      encoder.setXUASTCLDRBoundedRDOParam(i, dto.xuastcBoundedRDO[i]);

   encoder.setXUASTCLDRForceDisableRGBDualPlane(dto.xuastcDisableRGBDualPlane);
   encoder.setXUASTCLDRForceDisableSubsets(dto.xuastcDisableSubsets);
   encoder.setXUASTCLDRUseBlurring(dto.xuastcUseBlur);
   encoder.setXUASTCLDRSelectCompressor(dto.xuastcSelectCompressor);
   encoder.setXUASTCLDRHeavySubsetUsage(dto.xuastcHeavySubsetUsage);
   encoder.setXUASTCLDRSharpenMode(dto.xuastcSharpenMode);
   encoder.setXUASTCLDRSharpenAmount(dto.xuastcSharpenAmount);
   encoder.setXUASTCLDRDeblockingMode(dto.xuastcDeblockingMode);
   encoder.setXUASTCLDRNumDeblockingPasses(dto.xuastcNumDeblockingPasses);
   encoder.setASTCOrXUASTCLDRWeights(dto.weights[0], dto.weights[1], dto.weights[2], dto.weights[3]);
   encoder.setSwizzle(dto.swizzle[0], dto.swizzle[1], dto.swizzle[2], dto.swizzle[3]);
   encoder.setXUASTCLDRSyntax(dto.xuastcSyntax);
   encoder.setXUBC7RDOLevel(dto.xubc7RDOLevel);
   encoder.setXUBC7NumStripes(dto.xubc7NumStripes);
   encoder.setXUBC7Encoder(dto.xubc7Encoder);
   encoder.setXUBC7BC7EScalarLevel(dto.xubc7BC7EScalarLevel);

   // mipmaps
   encoder.setMipGen(dto.mipGen);
   encoder.setMipFilter(dto.mipFilter);
   encoder.setMipScale(dto.mipScale);
   encoder.setMipSmallestDimension(dto.mipSmallestDim);
   encoder.setMipRenormalize(dto.mipRenormalize);
   encoder.setMipWrapping(dto.mipWrapping);
   encoder.setYFlip(dto.yFlip);

   // unified quality/effort LAST -- it intentionally overrides some of the
   // codec-specific low-level options set above (matches the legacy path)
   if (dto.applyUnified)
      encoder.setFormatModeAndQualityEffort(dto.formatMode, dto.unifiedQuality, dto.unifiedEffort, true);
}

// applyDDSEncodeDTO(encoder, dto): applies ONLY the DDS-export-specific options on top of a
// BasisEncoder that has ALREADY had applyEncodeDTO() run on it. The caller then calls
// encoder.encodeToDDS(buffer). Used by the DDS-export path (worker 'encodeDDS' message).
//
// PURE: depends only on (encoder, dto) -- no Module, no DOM (same contract as applyEncodeDTO).
//
// Everything DDS shares with KTX2 (source image, sRGB transfer func, perceptual, channel weights,
// mips, etc.) was already set by applyEncodeDTO() and is consumed by encodeToDDS()/build_dds()
// unchanged -- so we only set the genuinely NEW things here: the output format + BC7 packer knobs.
function applyDDSEncodeDTO(encoder, dto)
{
   encoder.setDDSFormatEnum(dto.ddsFormat);
   encoder.setDDSBC7Encoder(dto.ddsBC7Encoder);
   encoder.setDDSBC7FLevel(dto.ddsBC7FLevel);
   encoder.setDDSBC7EScalarLevel(dto.ddsBC7EScalarLevel);
}
