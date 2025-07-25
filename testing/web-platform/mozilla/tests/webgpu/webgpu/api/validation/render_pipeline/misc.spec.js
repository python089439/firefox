/**
* AUTO-GENERATED - DO NOT EDIT. Source: https://github.com/gpuweb/cts
**/export const description = `
misc createRenderPipeline and createRenderPipelineAsync validation tests.
`;import { makeTestGroup } from '../../../../common/framework/test_group.js';
import {
  isTextureFormatUsableWithStorageAccessMode,
  kPossibleStorageTextureFormats } from
'../../../format_info.js';
import { kDefaultVertexShaderCode, kDefaultFragmentShaderCode } from '../../../util/shader.js';
import * as vtu from '../validation_test_utils.js';

import { CreateRenderPipelineValidationTest } from './common.js';

export const g = makeTestGroup(CreateRenderPipelineValidationTest);

g.test('basic').
desc(`Test basic usage of createRenderPipeline.`).
params((u) => u.combine('isAsync', [false, true])).
fn((t) => {
  const { isAsync } = t.params;
  const descriptor = t.getDescriptor();

  vtu.doCreateRenderPipelineTest(t, isAsync, true, descriptor);
});

g.test('no_attachment').
desc(`Test that createRenderPipeline fails without any attachment.`).
params((u) => u.combine('isAsync', [false, true])).
fn((t) => {
  const { isAsync } = t.params;

  const descriptor = t.getDescriptor({
    noFragment: true,
    depthStencil: undefined
  });

  vtu.doCreateRenderPipelineTest(t, isAsync, false, descriptor);
});

g.test('vertex_state_only').
desc(
  `Tests creating vertex-state-only render pipeline. A vertex-only render pipeline has no fragment
state (and thus has no color state), and must have a depth-stencil state as an attachment is required.`
).
params((u) =>
u.
combine('isAsync', [false, true]).
beginSubcases().
combine('depthStencilFormat', [
'depth24plus',
'depth24plus-stencil8',
'depth32float',
'']
).
combine('hasColor', [false, true]).
unless(({ depthStencilFormat, hasColor }) => {
  // Render pipeline needs at least one attachement
  return hasColor === false && depthStencilFormat === '';
})
).
fn((t) => {
  const { isAsync, depthStencilFormat, hasColor } = t.params;

  let depthStencilState;
  if (depthStencilFormat === '') {
    depthStencilState = undefined;
  } else {
    depthStencilState = {
      format: depthStencilFormat,
      depthWriteEnabled: false,
      depthCompare: 'always'
    };
  }

  // Having targets or not should have no effect in result, since it will not appear in the
  // descriptor in vertex-only render pipeline
  const descriptor = t.getDescriptor({
    noFragment: true,
    depthStencil: depthStencilState,
    targets: hasColor ? [{ format: 'rgba8unorm' }] : []
  });

  vtu.doCreateRenderPipelineTest(t, isAsync, depthStencilState !== undefined, descriptor);
});

g.test('pipeline_layout,device_mismatch').
desc(
  'Tests createRenderPipeline(Async) cannot be called with a pipeline layout created from another device'
).
paramsSubcasesOnly((u) => u.combine('isAsync', [true, false]).combine('mismatched', [true, false])).
beforeAllSubcases((t) => t.usesMismatchedDevice()).
fn((t) => {
  const { isAsync, mismatched } = t.params;

  const sourceDevice = mismatched ? t.mismatchedDevice : t.device;

  const layout = sourceDevice.createPipelineLayout({ bindGroupLayouts: [] });

  const format = 'rgba8unorm';
  const descriptor = {
    layout,
    vertex: {
      module: t.device.createShaderModule({
        code: kDefaultVertexShaderCode
      }),
      entryPoint: 'main'
    },
    fragment: {
      module: t.device.createShaderModule({
        code: kDefaultFragmentShaderCode
      }),
      entryPoint: 'main',
      targets: [{ format }]
    }
  };

  vtu.doCreateRenderPipelineTest(t, isAsync, !mismatched, descriptor);
});

g.test('external_texture').
desc('Tests createRenderPipeline() with an external_texture').
fn((t) => {
  const shader = t.device.createShaderModule({
    code: `
        @vertex
        fn vertexMain() -> @builtin(position) vec4f {
          return vec4f(1);
        }

        @group(0) @binding(0) var myTexture: texture_external;

        @fragment
        fn fragmentMain() -> @location(0) vec4f {
          let result = textureLoad(myTexture, vec2u(1, 1));
          return vec4f(1);
        }
      `
  });

  const descriptor = {
    layout: 'auto',
    vertex: {
      module: shader
    },
    fragment: {
      module: shader,
      targets: [{ format: 'rgba8unorm' }]
    }
  };

  vtu.doCreateRenderPipelineTest(t, false, true, descriptor);
});

g.test('storage_texture,format').
desc(
  `
Test that a pipeline with auto layout and storage texture access combo that is not supported
generates a validation error at createComputePipeline(Async)
  `
).
params((u) =>
u //
.combine('format', kPossibleStorageTextureFormats).
beginSubcases().
combine('isAsync', [true, false]).
combine('access', ['read', 'write', 'read_write']).
combine('dimension', ['1d', '2d', '3d'])
).
fn((t) => {
  const { format, isAsync, access, dimension } = t.params;
  t.skipIfTextureFormatNotSupported(format);

  const code = `
      @group(0) @binding(0) var tex: texture_storage_${dimension}<${format}, ${access}>;
      @vertex fn vs() -> @builtin(position) vec4f {
        return vec4f(0);
      }

      @fragment fn fs() -> @location(0) vec4f {
        _ = tex;
        return vec4f(0);
      }
    `;
  const module = t.device.createShaderModule({ code });

  const success = isTextureFormatUsableWithStorageAccessMode(t.device, format, access);
  const descriptor = {
    layout: 'auto',
    vertex: { module },
    fragment: { module, targets: [{ format: 'rgba8unorm' }] }
  };
  vtu.doCreateRenderPipelineTest(t, isAsync, success, descriptor);
});