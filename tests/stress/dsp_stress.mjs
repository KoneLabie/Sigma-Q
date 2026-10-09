// Standalone port of the SVF equations and the 32-sample motion update in
// Sigma-Q commit b51ac8e (Source/Dsp.h and Source/PluginProcessor.cpp).
// This exercises source math; it does not load the VST3 or JUCE.

const sampleRate = 44100;

function makeSvf(kind, frequency, q, gainDb = 0) {
  const A = 10 ** (gainDb / 40);
  const g = Math.tan(Math.PI * frequency / sampleRate);
  const k = kind === 'bell' ? 1 / (q * A) : 1 / q;
  const a1 = 1 / (1 + g * (g + k));
  const a2 = g * a1;
  const a3 = g * a2;
  return {
    a1, a2, a3,
    m0: kind === 'bell' ? 1 : 0,
    m1: kind === 'bell' ? k * (A * A - 1) : 0,
    m2: kind === 'bell' ? 0 : 1,
  };
}

function processSvf(state, coeff, input) {
  const v3 = input - state.ic2;
  const v1 = coeff.a1 * state.ic1 + coeff.a2 * v3;
  const v2 = state.ic2 + coeff.a2 * state.ic1 + coeff.a3 * v3;
  state.ic1 = 2 * v1 - state.ic1;
  state.ic2 = 2 * v2 - state.ic2;
  return coeff.m0 * input + coeff.m1 * v1 + coeff.m2 * v2;
}

function bypassFrozenState() {
  // One band: High Cut, 20 Hz, 24 dB/oct (two Butterworth stages).
  const states = [{ ic1: 0, ic2: 0 }, { ic1: 0, ic2: 0 }];
  const coeffs = [0.5411961001461970, 1.3065629648763766]
    .map(q => makeSvf('lowpass', 20, q));
  function process(input) {
    let value = input;
    for (let stage = 0; stage < states.length; stage++)
      value = processSvf(states[stage], coeffs[stage], value);
    return value;
  }
  for (let i = 0; i < 2 * sampleRate; i++) process(1);
  // Processor skips processSub while bypassed. One second of silence arrives.
  // No state update occurs in these 44,100 samples.
  const resumed = [];
  for (let i = 0; i <= 1000; i++) {
    const value = process(0);
    if ([0, 1, 31, 100, 1000].includes(i)) resumed.push([i, value]);
  }
  return resumed;
}

function sawMotionWrap() {
  // One band: Bell, 1 kHz, Q=1, 0 dB base gain, LFO Saw, Sync off,
  // 10 Hz, 0-octave sweep, 18 dB swell, 0.2-amplitude 1-kHz sine input.
  const state = { ic1: 0, ic2: 0 };
  const output = new Float64Array(sampleRate);
  const subBlock = 32;
  let cycles = 0;
  for (let start = 0; start < output.length; start += subBlock) {
    const count = Math.min(subBlock, output.length - start);
    cycles += 10 * count / sampleRate;
    const fraction = cycles - Math.floor(cycles);
    const saw = 2 * fraction - 1;
    const coeff = makeSvf('bell', 1000, 1, 18 * saw);
    for (let i = start; i < start + count; i++) {
      const input = 0.2 * Math.sin(2 * Math.PI * 1000 * i / sampleRate);
      output[i] = processSvf(state, coeff, input);
    }
  }
  let at = 1;
  for (let i = 2; i < output.length; i++)
    if (Math.abs(output[i] - output[i - 1]) > Math.abs(output[at] - output[at - 1])) at = i;
  return {
    sample: at,
    before: output[at - 1],
    after: output[at],
    jump: output[at] - output[at - 1],
    maxDryAdjacentJump: 0.4 * Math.sin(Math.PI * 1000 / sampleRate),
  };
}

console.log(JSON.stringify({ bypass: bypassFrozenState(), saw: sawMotionWrap() }, null, 2));
