export const description = `
copyImageBitmapToTexture from ImageBitmaps created from various sources.

TODO: Test ImageBitmap generated from all possible ImageBitmapSource, relevant ImageBitmapOptions
    (https://html.spec.whatwg.org/multipage/imagebitmap-and-animations.html#images-2)
    and various source filetypes and metadata (weird dimensions, EXIF orientations, video rotations
    and visible/crop rectangles, etc. (In theory these things are handled inside createImageBitmap,
    but in theory could affect the internal representation of the ImageBitmap.)

TODO: Test zero-sized copies from all sources (just make sure params cover it) (e.g. 0x0, 0x4, 4x0).
`;

import { poptions, params } from '../../../common/framework/params_builder.js';
import { makeTestGroup } from '../../../common/framework/test_group.js';
import { unreachable } from '../../../common/framework/util/util.js';
import {
  kUncompressedTextureFormatInfo,
  UncompressedTextureFormat,
} from '../../capability_info.js';
import { GPUTest } from '../../gpu_test.js';
import { kTexelRepresentationInfo } from '../../util/texture/texel_data.js';

function calculateRowPitch(width: number, bytesPerPixel: number): number {
  const bytesPerRow = width * bytesPerPixel;
  // Rounds up to a multiple of 256 according to WebGPU requirements.
  return (((bytesPerRow - 1) >> 8) + 1) << 8;
}

enum Color {
  Red,
  Green,
  Blue,
  White,
  OpaqueBlack,
  TransparentBlack,
}
// Cache for generated pixels.
const generatedPixelCache: Map<GPUTextureFormat, Map<Color, Uint8Array>> = new Map();

class F extends GPUTest {
  checkCopyImageBitmapResult(
    src: GPUBuffer,
    expected: ArrayBufferView,
    width: number,
    height: number,
    bytesPerPixel: number
  ): void {
    const exp = new Uint8Array(expected.buffer, expected.byteOffset, expected.byteLength);
    const rowPitch = calculateRowPitch(width, bytesPerPixel);
    const dst = this.createCopyForMapRead(src, 0, rowPitch * height);

    this.eventualAsyncExpectation(async niceStack => {
      await dst.mapAsync(GPUMapMode.READ);
      const actual = new Uint8Array(dst.getMappedRange());
      const check = this.checkBufferWithRowPitch(
        actual,
        exp,
        width,
        height,
        rowPitch,
        bytesPerPixel
      );
      if (check !== undefined) {
        niceStack.message = check;
        this.rec.expectationFailed(niceStack);
      }
      dst.destroy();
    });
  }

  checkBufferWithRowPitch(
    actual: Uint8Array,
    exp: Uint8Array,
    width: number,
    height: number,
    rowPitch: number,
    bytesPerPixel: number
  ): string | undefined {
    const failedByteIndices: string[] = [];
    const failedByteExpectedValues: string[] = [];
    const failedByteActualValues: string[] = [];
    iLoop: for (let i = 0; i < height; ++i) {
      const bytesPerRow = width * bytesPerPixel;
      for (let j = 0; j < bytesPerRow; ++j) {
        const indexExp = j + i * bytesPerRow;
        const indexActual = j + rowPitch * i;
        if (actual[indexActual] !== exp[indexExp]) {
          if (failedByteIndices.length >= 4) {
            failedByteIndices.push('...');
            failedByteExpectedValues.push('...');
            failedByteActualValues.push('...');
            break iLoop;
          }
          failedByteIndices.push(`(${i},${j})`);
          failedByteExpectedValues.push(exp[indexExp].toString());
          failedByteActualValues.push(actual[indexActual].toString());
        }
      }
    }
    if (failedByteIndices.length > 0) {
      return `at [${failedByteIndices.join(', ')}], \
expected [${failedByteExpectedValues.join(', ')}], \
got [${failedByteActualValues.join(', ')}]`;
    }
    return undefined;
  }

  doTestAndCheckResult(
    imageBitmapCopyView: GPUImageBitmapCopyView,
    dstTextureCopyView: GPUTextureCopyView,
    copySize: GPUExtent3D,
    bytesPerPixel: number,
    expectedData: Uint8ClampedArray
  ): void {
    this.device.queue.copyImageBitmapToTexture(imageBitmapCopyView, dstTextureCopyView, copySize);

    const imageBitmap = imageBitmapCopyView.imageBitmap;
    const dstTexture = dstTextureCopyView.texture;

    const bytesPerRow = calculateRowPitch(imageBitmap.width, bytesPerPixel);
    const testBuffer = this.device.createBuffer({
      size: bytesPerRow * imageBitmap.height,
      usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
    });

    const encoder = this.device.createCommandEncoder();

    encoder.copyTextureToBuffer(
      { texture: dstTexture, mipLevel: 0, origin: { x: 0, y: 0, z: 0 } },
      { buffer: testBuffer, bytesPerRow },
      { width: imageBitmap.width, height: imageBitmap.height, depthOrArrayLayers: 1 }
    );
    this.device.queue.submit([encoder.finish()]);

    this.checkCopyImageBitmapResult(
      testBuffer,
      expectedData,
      imageBitmap.width,
      imageBitmap.height,
      bytesPerPixel
    );
  }

  generatePixel(color: Color, format: UncompressedTextureFormat): Uint8Array {
    let entry = generatedPixelCache.get(format);
    if (entry === undefined) {
      entry = new Map();
      generatedPixelCache.set(format, entry);
    }

    // None of the dst texture format is 'uint' or 'sint', so we can always use float value.
    if (!entry.has(color)) {
      const rep = kTexelRepresentationInfo[format];
      let pixels;
      switch (color) {
        case Color.Red:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 1.0, G: 0, B: 0, A: 1.0 })));
          break;
        case Color.Green:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 0, G: 1.0, B: 0, A: 1.0 })));
          break;
        case Color.Blue:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 0, G: 0, B: 1.0, A: 1.0 })));
          break;
        case Color.White:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 0, G: 0, B: 0, A: 1.0 })));
          break;
        case Color.OpaqueBlack:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 1.0, G: 1.0, B: 1.0, A: 1.0 })));
          break;
        case Color.TransparentBlack:
          pixels = new Uint8Array(rep.pack(rep.encode({ R: 1.0, G: 1.0, B: 1.0, A: 0 })));
          break;
        default:
          unreachable();
      }
      entry.set(color, pixels);
    }

    return entry.get(color)!;
  }
}

export const g = makeTestGroup(F);

g.test('from_ImageData')
  .cases(
    params()
      .combine(poptions('alpha', ['none', 'premultiply']))
      .combine(poptions('orientation', ['none', 'flipY']))
      .combine(
        poptions('dstColorFormat', [
          'rgba8unorm',
          'bgra8unorm',
          'rgba8unorm-srgb',
          'bgra8unorm-srgb',
          'rgb10a2unorm',
          'rgba16float',
          'rgba32float',
          'rg8unorm',
          'rg16float',
        ] as const)
      )
  )
  .subcases(() =>
    params()
      .combine(poptions('width', [1, 2, 4, 15, 255, 256]))
      .combine(poptions('height', [1, 2, 4, 15, 255, 256]))
  )
  .fn(async t => {
    const { width, height, alpha, orientation, dstColorFormat } = t.params;

    const format = 'rgba8unorm';
    const srcBytesPerPixel = kUncompressedTextureFormatInfo[format].bytesPerBlock;

    // Generate input contents by iterating 'Color' enum
    const imagePixels = new Uint8ClampedArray(srcBytesPerPixel * width * height);
    const startPixel = Color.Red;
    for (let i = 0, currentPixel = startPixel; i < width * height; ++i) {
      const pixels = t.generatePixel(currentPixel, format);
      if (currentPixel === Color.TransparentBlack) {
        currentPixel = Color.Red;
      } else {
        ++currentPixel;
      }
      for (let j = 0; j < srcBytesPerPixel; ++j) {
        imagePixels[i * srcBytesPerPixel + j] = pixels[j];
      }
    }

    // Generate correct expected values
    const imageData = new ImageData(imagePixels, width, height);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const imageBitmap = await (createImageBitmap as any)(imageData, {
      premultiplyAlpha: alpha,
      imageOrientation: orientation,
    });

    const dst = t.device.createTexture({
      size: {
        width: imageBitmap.width,
        height: imageBitmap.height,
        depthOrArrayLayers: 1,
      },
      format: dstColorFormat,
      usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC,
    });

    // Construct expected value for different dst color format
    const dstBytesPerPixel = kUncompressedTextureFormatInfo[dstColorFormat].bytesPerBlock!;
    const dstPixels = new Uint8ClampedArray(dstBytesPerPixel * width * height);
    let expectedPixels = new Uint8ClampedArray(dstBytesPerPixel * width * height);
    for (let i = 0, currentPixel = startPixel; i < width * height; ++i) {
      const pixels = t.generatePixel(currentPixel, dstColorFormat);
      for (let j = 0; j < dstBytesPerPixel; ++j) {
        // All pixels are 0 due to premultiply alpha
        if (alpha === 'premultiply' && currentPixel === Color.TransparentBlack) {
          dstPixels[i * dstBytesPerPixel + j] = 0;
        } else {
          dstPixels[i * dstBytesPerPixel + j] = pixels[j];
        }
      }

      if (currentPixel === Color.TransparentBlack) {
        currentPixel = Color.Red;
      } else {
        ++currentPixel;
      }
    }

    if (orientation === 'flipY') {
      for (let i = 0; i < height; ++i) {
        for (let j = 0; j < width * dstBytesPerPixel; ++j) {
          const posImagePixel = (height - i - 1) * width * dstBytesPerPixel + j;
          const posExpectedValue = i * width * dstBytesPerPixel + j;
          expectedPixels[posExpectedValue] = dstPixels[posImagePixel];
        }
      }
    } else {
      expectedPixels = dstPixels;
    }

    t.doTestAndCheckResult(
      { imageBitmap, origin: { x: 0, y: 0 } },
      { texture: dst },
      { width: imageBitmap.width, height: imageBitmap.height, depthOrArrayLayers: 1 },
      dstBytesPerPixel,
      expectedPixels
    );
  });

g.test('from_canvas')
  .subcases(() =>
    params()
      .combine(poptions('width', [1, 2, 4, 15, 255, 256]))
      .combine(poptions('height', [1, 2, 4, 15, 255, 256]))
  )
  .fn(async t => {
    const { width, height } = t.params;

    // CTS sometimes runs on worker threads, where document is not available.
    // In this case, OffscreenCanvas can be used instead of <canvas>.
    // But some browsers don't support OffscreenCanvas, and some don't
    // support '2d' contexts on OffscreenCanvas.
    // In this situation, the case will be skipped.
    let imageCanvas;
    if (typeof document !== 'undefined') {
      imageCanvas = document.createElement('canvas');
      imageCanvas.width = width;
      imageCanvas.height = height;
    } else if (typeof OffscreenCanvas === 'undefined') {
      t.skip('OffscreenCanvas is not supported');
      return;
    } else {
      imageCanvas = new OffscreenCanvas(width, height);
    }
    const imageCanvasContext = imageCanvas.getContext('2d');
    if (imageCanvasContext === null) {
      t.skip('OffscreenCanvas "2d" context not available');
      return;
    }

    // The texture format is rgba8unorm, so the bytes per pixel is 4.
    const bytesPerPixel = 4;

    // Generate original data.
    const imagePixels = new Uint8ClampedArray(bytesPerPixel * width * height);
    for (let i = 0; i < width * height * bytesPerPixel; ++i) {
      imagePixels[i] = i % 4 === 3 ? 255 : i % 256;
    }

    const imageData = new ImageData(imagePixels, width, height);
    imageCanvasContext.putImageData(imageData, 0, 0);

    const imageBitmap = await createImageBitmap(imageCanvas);

    const dst = t.device.createTexture({
      size: {
        width: imageBitmap.width,
        height: imageBitmap.height,
        depthOrArrayLayers: 1,
      },
      format: 'rgba8unorm',
      usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC,
    });

    // This will get origin data and even it has premultiplied-alpha
    const expectedData = imageCanvasContext.getImageData(
      0,
      0,
      imageBitmap.width,
      imageBitmap.height
    ).data;

    t.doTestAndCheckResult(
      { imageBitmap, origin: { x: 0, y: 0 } },
      { texture: dst },
      { width: imageBitmap.width, height: imageBitmap.height, depthOrArrayLayers: 1 },
      bytesPerPixel,
      expectedData
    );
  });
