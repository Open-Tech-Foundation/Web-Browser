export default {
  module: 'webgpu',
  category: 'security',
  produces: [{
    id: 'webgpu-compute',
    label: 'WebGPU compute pipeline',
    entropy: 'security',
    description: 'Whether arbitrary compute shaders run — abuse vector for crypto mining.',
  }],
  async run(ctx) {
    const diag = [
      ['policy injected', String(!!globalThis.__otfPagePolicyInjected)],
    ];
    const state = globalThis.__otfWebGPUPolicyState;
    if (state) {
      diag.push(['policy saw GPU', String(state.hadGPU)]);
      diag.push(['policy saw GPUAdapter', String(state.hadGPUAdapter)]);
      diag.push(['policy saw GPUDevice', String(state.hadGPUDevice)]);
    } else {
      diag.push(['policy WebGPU state', 'missing']);
    }
    try {
      const GPUProto = globalThis.GPU && globalThis.GPU.prototype;
      const GPUAdapterProto = globalThis.GPUAdapter && globalThis.GPUAdapter.prototype;
      const GPUDeviceProto = globalThis.GPUDevice && globalThis.GPUDevice.prototype;
      diag.push(['GPU patched', String(!!(GPUProto && GPUProto.__otfGPUPolicy))]);
      diag.push(['GPUAdapter patched', String(!!(GPUAdapterProto && GPUAdapterProto.__otfGPUAdapterPolicy))]);
      diag.push(['GPUDevice patched', String(!!(GPUDeviceProto && GPUDeviceProto.__otfWebGPUComputePolicy))]);
    } catch (_) {}

    if (!navigator.gpu) {
      ctx.set('webgpu-compute', 'ok', 'WebGPU unavailable',
        'Compute pipeline surface is not exposed in this runtime.',
        [['navigator.gpu', 'false'], ...diag]);
      return;
    }
    try {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) {
        ctx.set('webgpu-compute', 'ok', 'No WebGPU adapter',
          'Compute pipeline surface is not available without an adapter.',
          [['adapter', 'null'], ...diag]);
        return;
      }
      const device = await adapter.requestDevice();
      const shader = device.createShaderModule({ code: '@compute @workgroup_size(1) fn main() {}' });
      device.createComputePipeline({ layout: 'auto', compute: { module: shader, entryPoint: 'main' } });
      ctx.set('webgpu-compute', 'fail', 'Compute pipeline allowed',
        'createComputePipeline succeeded.',
        [['navigator.gpu', 'true'], ['error', 'none'], ...diag]);
    } catch (error) {
      const expected = /compute pipelines are disabled/i.test(error.message);
      ctx.set('webgpu-compute', expected ? 'ok' : 'warn',
        expected ? 'Compute pipeline blocked' : 'Compute blocked by runtime',
        `${error.name}: ${error.message}`,
        [
          ['navigator.gpu', 'true'],
          ['error name', error.name],
          ['error message', error.message],
          ...diag,
        ]);
    }
  },
};
