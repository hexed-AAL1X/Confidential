#!/usr/bin/env python3
"""Wraps the exported GLB into a single self-contained HTML viewer (works from file://)."""
import base64
import json
import os
import sys

import numpy as np

glb_path = sys.argv[1] if len(sys.argv) > 1 else "build/little_st_james.glb"
out_path = sys.argv[2] if len(sys.argv) > 2 else "build/isla_3d.html"

with open(glb_path, "rb") as f:
    glb_b64 = base64.b64encode(f.read()).decode("ascii")

Z_SCALE = 1.6
island = json.load(open(os.path.join(os.path.dirname(glb_path), "island.json")))
cx = island["xmin"] + island["size"] / 2
cy = island["ymin"] + island["size"] / 2


def focus(b):
    ring = b["ring"]
    x = sum(p[0] for p in ring) / len(ring) - cx
    y = sum(p[1] for p in ring) / len(ring) - cy
    return [round(x, 1), round(b["base"] * Z_SCALE + 3, 1), round(-y, 1)]


def area(ring):
    return abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(ring, ring[1:] + ring[:1])))


bed = np.load(os.path.join(os.path.dirname(glb_path), "height.npy")).astype(np.float32) * Z_SCALE
sim = {"n": int(bed.shape[0]), "dx": island["step"], "size": island["size"]}
bed_b64 = base64.b64encode(np.ascontiguousarray(bed).tobytes()).decode("ascii")

houses = [b for b in island["buildings"] if b["kind"] == "yes"]
temple = next(b for b in island["buildings"] if b["kind"] == "temple")
views = {"main": focus(max(houses, key=lambda b: area(b["ring"]))), "temple": focus(temple)}

html = """<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<title>Little St. James - 3D</title>
<style>
  html, body { margin: 0; height: 100%; background: #0a0608; overflow: hidden; font-family: monospace; }
  #hud { position: fixed; left: 16px; top: 14px; color: #e0404a; font-size: 20px; letter-spacing: 2px; text-shadow: 0 2px 0 #000; }
  #help { position: fixed; left: 16px; bottom: 14px; color: #ccc; font-size: 12px; text-shadow: 0 1px 0 #000; }
  #credit { position: fixed; right: 16px; top: 18px; color: #999; font-size: 11px; }
  #loading { position: fixed; inset: 0; display: flex; align-items: center; justify-content: center; color: #e0404a; font-size: 22px; }
</style>
<script type="importmap">
{ "imports": {
  "three": "https://unpkg.com/three@0.160.0/build/three.module.js",
  "three/addons/": "https://unpkg.com/three@0.160.0/examples/jsm/"
} }
</script>
</head>
<body>
<div id="loading">CARGANDO ISLA...</div>
<div id="hud">LITTLE ST. JAMES</div>
<div id="help">Arrastrar: girar &nbsp;|&nbsp; Rueda: zoom &nbsp;|&nbsp; Clic derecho: mover &nbsp;|&nbsp; R: girar solo &nbsp;|&nbsp; 1/2/3: general / casas / templo &nbsp;|&nbsp; P: pixel art on/off &nbsp;|&nbsp; O: olas on/off &nbsp;|&nbsp; [ ]: tamano pixel <span id="px"></span></div>
<div id="credit">Mapa: &copy; OpenStreetMap contributors (ODbL) &middot; Relieve: AWS Terrain Tiles &middot; Altura x1.6</div>
<script type="module">
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { EffectComposer } from "three/addons/postprocessing/EffectComposer.js";
import { RenderPixelatedPass } from "three/addons/postprocessing/RenderPixelatedPass.js";
import { OutputPass } from "three/addons/postprocessing/OutputPass.js";
import { ShaderPass } from "three/addons/postprocessing/ShaderPass.js";

const GLB = "__GLB__";

const renderer = new THREE.WebGLRenderer({ antialias: false });
renderer.setPixelRatio(1);
renderer.domElement.style.imageRendering = "pixelated";
renderer.setSize(innerWidth, innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.shadowMap.enabled = true;
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x60a0df, 700, 2300);
const skyGeo = new THREE.SphereGeometry(3000, 32, 16);
skyGeo.attributes.normal.array.forEach((_, i, a) => { a[i] = i % 3 === 1 ? -1 : 0; });
scene.add(new THREE.Mesh(skyGeo, new THREE.MeshBasicMaterial({ color: 0x60a0df, side: THREE.BackSide, fog: false })));

const camera = new THREE.PerspectiveCamera(45, innerWidth / innerHeight, 1, 6000);
camera.position.set(-520, 480, 620);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0, -20);
controls.enableDamping = true;
controls.maxPolarAngle = Math.PI * 0.48;
controls.minDistance = 30;
controls.maxDistance = 2200;
controls.autoRotateSpeed = 0.6;

scene.add(new THREE.HemisphereLight(0xcfe6ff, 0x4a5a3a, 1.3));
const sun = new THREE.DirectionalLight(0xfff2dd, 2.6);
sun.position.set(-400, 600, 300);
sun.castShadow = true;
sun.shadow.mapSize.set(4096, 4096);
Object.assign(sun.shadow.camera, { left: -650, right: 650, top: 650, bottom: -650, near: 10, far: 2000 });
sun.shadow.bias = -0.0005;
scene.add(sun);

const bin = await (await fetch("data:application/octet-stream;base64," + GLB)).arrayBuffer();
let modelLoaded;
const modelReady = new Promise(resolve => { modelLoaded = resolve; });
new GLTFLoader().parse(bin, "", gltf => {
  gltf.scene.traverse(o => {
    if (o.isMesh) {
      o.material.metalness = Math.min(o.material.metalness, 0.2);
      o.castShadow = o.name !== "Water" && o.name !== "Terrain";
      o.receiveShadow = o.name !== "Water";
      if (o.name === "Water") o.visible = false;
    }
  });
  const ocean = new THREE.Mesh(
    new THREE.PlaneGeometry(4800, 4800).rotateX(-Math.PI / 2),
    new THREE.MeshStandardMaterial({ color: 0x124a7a, roughness: 0.95 })
  );
  ocean.position.y = -12;
  ocean.receiveShadow = true;
  scene.add(ocean);
  scene.add(gltf.scene);
  document.getElementById("loading").remove();
  modelLoaded();
}, err => { document.getElementById("loading").textContent = "ERROR: " + err; });

// Waves: depth-averaged Navier-Stokes (nonlinear shallow-water / Saint-Venant equations) on a staggered grid
// over the real bathymetry. State texture: r = surface elevation, g = u (east face), b = v (north face), a = foam.
const SIM = __SIM__;
const N = SIM.n, DX = SIM.dx, SIZE = SIM.size;
const bedData = new Float32Array(await (await fetch("data:application/octet-stream;base64,__BED__")).arrayBuffer());
const bedTex = new THREE.DataTexture(bedData, N, N, THREE.RedFormat, THREE.FloatType);
bedTex.magFilter = bedTex.minFilter = THREE.NearestFilter;
bedTex.needsUpdate = true;

const rtOpts = { type: THREE.FloatType, minFilter: THREE.NearestFilter, magFilter: THREE.NearestFilter, depthBuffer: false };
let stateA = new THREE.WebGLRenderTarget(N, N, rtOpts);
let stateB = new THREE.WebGLRenderTarget(N, N, rtOpts);
for (const rt of [stateA, stateB]) { renderer.setRenderTarget(rt); renderer.setClearColor(0x000000, 0); renderer.clear(); }
renderer.setRenderTarget(null);
renderer.setClearColor(0x000000, 1);

const G = 9.81, DEEP = 40.0;
const simU = {
  tState: { value: null }, tBed: { value: bedTex }, N: { value: N }, dx: { value: DX }, dt: { value: 1 / 60 },
  time: { value: 0 }, gravity: { value: G }, nu: { value: 1.2 }, cf: { value: 0.012 },
  waves: { value: [new THREE.Vector4(), new THREE.Vector4(), new THREE.Vector4()] },
  drop: { value: new THREE.Vector4() },
};
const SIM_COMMON = `
  uniform sampler2D tState, tBed; uniform float N, dx, dt, time, gravity, nu, cf;
  uniform vec4 waves[3]; uniform vec4 drop;
  varying vec2 vUv;
  vec4 S(float x, float y) { return texture2D(tState, vUv + vec2(x, y) / N); }
  float B(float x, float y) { return texture2D(tBed, vUv + vec2(x, y) / N).r; }
  float sponge() {
    vec2 c = vUv * N - 0.5;
    float edge = min(min(c.x, N - 1.0 - c.x), min(c.y, N - 1.0 - c.y));
    return 1.0 - smoothstep(0.0, 24.0, edge);
  }
  vec3 swell() {
    vec2 p = (vUv * N - 0.5) * dx;
    vec3 r = vec3(0.0);
    for (int k = 0; k < 3; k++) {
      vec4 w = waves[k];
      float kk = length(w.xy);
      float e = w.w * sin(dot(w.xy, p) - w.z * time);
      r.x += e;
      r.yz += (w.xy / kk) * e * gravity * kk / w.z;
    }
    return r;
  }
`;
const SIM_VS = `varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`;
const velocityMat = new THREE.ShaderMaterial({ uniforms: simU, vertexShader: SIM_VS, fragmentShader: SIM_COMMON + `
  void main() {
    vec4 s = S(0., 0.), sE = S(1., 0.), sN = S(0., 1.), sW = S(-1., 0.), sS = S(0., -1.);
    float b = B(0., 0.), bE = B(1., 0.), bN = B(0., 1.);
    float H = s.r - b;
    float wet = step(0.05, H), wetE = step(0.05, sE.r - bE), wetN = step(0.05, sN.r - bN);
    float u = s.g, v = s.b;
    float dudx = u > 0.0 ? (u - sW.g) / dx : (sE.g - u) / dx;
    float dudy = v > 0.0 ? (u - sS.g) / dx : (sN.g - u) / dx;
    float dvdx = u > 0.0 ? (v - sW.b) / dx : (sE.b - v) / dx;
    float dvdy = v > 0.0 ? (v - sS.b) / dx : (sN.b - v) / dx;
    float lapU = (sE.g + sW.g + sN.g + sS.g - 4.0 * u) / (dx * dx);
    float lapV = (sE.b + sW.b + sN.b + sS.b - 4.0 * v) / (dx * dx);
    float un = u + dt * (-gravity * (sE.r - s.r) / dx - (u * dudx + v * dudy) + nu * lapU);
    float vn = v + dt * (-gravity * (sN.r - s.r) / dx - (u * dvdx + v * dvdy) + nu * lapV);
    float Hc = max(H, 0.15);
    float drag = 1.0 + dt * cf * length(vec2(un, vn)) / Hc;
    float breaking = 1.0 - 0.12 * smoothstep(0.3, 0.6, abs(s.r) / Hc);
    float sp = sponge() * 0.08;
    vec3 sw = swell();
    un = mix(un / drag * breaking, sw.y, sp) * wet * wetE;
    vn = mix(vn / drag * breaking, sw.z, sp) * wet * wetN;
    gl_FragColor = vec4(s.r, un, vn, s.a);
  }` });
const heightMat = new THREE.ShaderMaterial({ uniforms: simU, vertexShader: SIM_VS, fragmentShader: SIM_COMMON + `
  void main() {
    vec4 s = S(0., 0.), sE = S(1., 0.), sN = S(0., 1.), sW = S(-1., 0.), sS = S(0., -1.);
    float b = B(0., 0.), bE = B(1., 0.), bN = B(0., 1.), bW = B(-1., 0.), bS = B(0., -1.);
    float HE = max(s.g > 0.0 ? s.r - b : sE.r - bE, 0.0);
    float HW = max(sW.g > 0.0 ? sW.r - bW : s.r - b, 0.0);
    float HN = max(s.b > 0.0 ? s.r - b : sN.r - bN, 0.0);
    float HS = max(sS.b > 0.0 ? sS.r - bS : s.r - b, 0.0);
    float eta = s.r - dt * (HE * s.g - HW * sW.g + HN * s.b - HS * sS.b) / dx;

    vec2 c = vUv * N - 0.5;
    float d2 = dot(c - drop.xy, c - drop.xy);
    eta += drop.w * exp(-d2 / (drop.z * drop.z));
    eta = mix(eta, swell().x, sponge() * 0.08);

    float H = eta - b;
    float land = step(-0.05, b);
    float shore = max(max(step(-0.05, bE), step(-0.05, bW)), max(step(-0.05, bN), step(-0.05, bS)));
    float conv = -(s.g - sW.g + s.b - sS.b) / dx;
    float speed = length(vec2(s.g, s.b));
    float gen = smoothstep(0.25, 0.55, abs(eta) / max(H, 0.15))
              + smoothstep(0.015, 0.06, conv) * step(H, 6.0)
              + shore * smoothstep(0.15, 0.5, speed);
    vec2 back = vUv - vec2(s.g, s.b) * dt / (dx * N);
    float foam = texture2D(tState, back).a * exp(-dt * 0.8);
    foam = clamp(foam + dt * 2.5 * gen, 0.0, 1.0);

    gl_FragColor = land > 0.5 ? vec4(0.0) : vec4(eta, s.g, s.b, foam);
  }` });
const simScene = new THREE.Scene();
const simCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
const simQuad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), velocityMat);
simScene.add(simQuad);

function simStep() {
  simU.tState.value = stateA.texture;
  simQuad.material = velocityMat;
  renderer.setRenderTarget(stateB);
  renderer.render(simScene, simCam);
  simU.tState.value = stateB.texture;
  simQuad.material = heightMat;
  renderer.setRenderTarget(stateA);
  renderer.render(simScene, simCam);
  renderer.setRenderTarget(null);
  simU.time.value += simU.dt.value;
  simU.drop.value.w = 0;
}

// Random sea state: a swell whose direction slowly wanders, plus random gusts dropping wave packets.
let swellDir = Math.random() * Math.PI * 2;
const swellParts = [0, 1, 2].map(k => ({
  off: (k - 1) * (0.25 + Math.random() * 0.2),
  lambda: 55 + Math.random() * 60,
  amp: 0.12 + Math.random() * 0.12,
}));
function updateSwell() {
  swellDir += (Math.random() - 0.5) * 0.004;
  swellParts.forEach((p, k) => {
    const kk = 2 * Math.PI / p.lambda;
    const th = swellDir + p.off;
    simU.waves.value[k].set(Math.cos(th) * kk, Math.sin(th) * kk, Math.sqrt(G * DEEP) * kk, p.amp);
  });
}
let nextDrop = 0;
function maybeDrop() {
  if (simU.time.value < nextDrop) return;
  nextDrop = simU.time.value + 0.4 + Math.random() * 1.6;
  for (let tries = 0; tries < 20; tries++) {
    const i = Math.floor(Math.random() * N), j = Math.floor(Math.random() * N);
    if (bedData[j * N + i] < -4) {
      simU.drop.value.set(i, j, 3 + Math.random() * 5, (Math.random() < 0.5 ? -1 : 1) * (0.25 + Math.random() * 0.5));
      return;
    }
  }
}

const VEX = 2.0;
const waterMat = new THREE.ShaderMaterial({
  transparent: true, depthWrite: false, fog: true,
  uniforms: THREE.UniformsUtils.merge([THREE.UniformsLib.fog, {
    tSim: { value: null }, N: { value: N }, SIZE: { value: SIZE }, dx: { value: DX }, VEX: { value: VEX },
    sunDir: { value: sun.position.clone().normalize() },
    waterColor: { value: new THREE.Color(0x2896af) },
  }]),
  vertexShader: `
    #include <fog_pars_vertex>
    uniform sampler2D tSim; uniform float N, SIZE, VEX;
    varying vec2 vUv; varying float vInside; varying vec3 vWorld;
    void main() {
      vec4 wp = modelMatrix * vec4(position, 1.0);
      vec2 t = vec2(wp.x, -wp.z) / SIZE + 0.5;
      vUv = (t * (N - 1.0) + 0.5) / N;
      vInside = step(0.0, t.x) * step(t.x, 1.0) * step(0.0, t.y) * step(t.y, 1.0);
      wp.y += texture2D(tSim, vUv).r * vInside * VEX;
      vWorld = wp.xyz;
      vec4 mvPosition = viewMatrix * wp;
      gl_Position = projectionMatrix * mvPosition;
      #include <fog_vertex>
    }`,
  fragmentShader: `
    #include <fog_pars_fragment>
    uniform sampler2D tSim; uniform float N, dx, VEX; uniform vec3 sunDir, waterColor;
    varying vec2 vUv; varying float vInside; varying vec3 vWorld;
    void main() {
      vec2 o = vec2(1.0 / N, 0.0);
      float hL = texture2D(tSim, vUv - o.xy).r, hR = texture2D(tSim, vUv + o.xy).r;
      float hD = texture2D(tSim, vUv - o.yx).r, hU = texture2D(tSim, vUv + o.yx).r;
      float slope = VEX * 5.0 / (2.0 * dx) * vInside;
      vec3 n = normalize(vec3(-(hR - hL) * slope, 1.0, (hU - hD) * slope));
      vec3 viewDir = normalize(cameraPosition - vWorld);
      float diff = max(dot(n, sunDir), 0.0);
      float spec = pow(max(dot(reflect(-sunDir, n), viewDir), 0.0), 40.0);
      float foam = texture2D(tSim, vUv).a * vInside;
      vec3 col = waterColor * (0.45 + 0.6 * diff) + vec3(spec * 0.7);
      col = mix(col, vec3(0.95, 0.97, 1.0), smoothstep(0.08, 0.5, foam));
      gl_FragColor = vec4(col, mix(0.5 + spec * 0.4, 0.95, smoothstep(0.08, 0.5, foam)));
      #include <fog_fragment>
      #include <colorspace_fragment>
    }`,
});
// One seamless sheet: the simulation grid, with its border vertices pushed out to the horizon.
const waterGeo = new THREE.PlaneGeometry(SIZE, SIZE, N - 1, N - 1).rotateX(-Math.PI / 2);
const wp = waterGeo.attributes.position;
for (let i = 0; i < wp.count; i++) {
  for (const axis of [0, 2]) {
    const c = wp.getComponent(i, axis);
    if (Math.abs(Math.abs(c) - SIZE / 2) < 1e-3) wp.setComponent(i, axis, Math.sign(c) * 2400);
  }
}
const simWater = new THREE.Mesh(waterGeo, waterMat);
simWater.frustumCulled = false;
scene.add(simWater);

let wavesOn = true;
function stepWaves() {
  if (!wavesOn) return;
  updateSwell();
  maybeDrop();
  for (let k = 0; k < 2; k++) simStep();
  waterMat.uniforms.tSim.value = stateA.texture;
}

const params = new URLSearchParams(location.search);
for (let i = 0; i < (+params.get("warm") || 0); i++) { updateSwell(); maybeDrop(); simStep(); }
if (params.has("clean")) for (const id of ["hud", "help", "credit"]) document.getElementById(id).style.display = "none";

// Pixel art: low-res render with edge outlines, then a limited, ordered-dithered palette on the pixel grid.
let pixelSize = 4;
let pixelArt = true;
const composer = new EffectComposer(renderer);
const pixelPass = new RenderPixelatedPass(pixelSize, scene, camera, { normalEdgeStrength: 0.4, depthEdgeStrength: 0.6 });
composer.addPass(pixelPass);
composer.addPass(new OutputPass());
const palettePass = new ShaderPass({
  uniforms: { tDiffuse: { value: null }, levels: { value: 9.0 }, pixel: { value: pixelSize } },
  vertexShader: `varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float levels; uniform float pixel; varying vec2 vUv;
    float bayer(vec2 p) {
      int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
      int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
      return float(m[y * 4 + x]) / 16.0;
    }
    void main() {
      vec3 c = texture2D(tDiffuse, vUv).rgb;
      float d = 0.5 + (bayer(floor(gl_FragCoord.xy / pixel)) - 0.5) * 0.4;
      c = floor(c * (levels - 1.0) + d) / (levels - 1.0);
      gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
    }`,
});
composer.addPass(palettePass);

function setPixelSize(n) {
  pixelSize = Math.max(2, Math.min(10, n));
  pixelPass.setPixelSize(pixelSize);
  palettePass.uniforms.pixel.value = pixelSize;
  document.getElementById("px").textContent = pixelArt ? "(" + pixelSize + ")" : "(off)";
}
setPixelSize(pixelSize);

const VIEWS = __VIEWS__;
function goTo(view) {
  if (view.startsWith("cam=")) {
    const [x, y, z, tx, ty, tz] = view.slice(4).split(",").map(Number);
    camera.position.set(x, y, z);
    controls.target.set(tx, ty, tz);
  } else if (view === "main" || view === "temple") {
    const [x, y, z] = VIEWS[view];
    controls.target.set(x, y, z);
    const k = view === "temple" ? 0.55 : 1.0;
    camera.position.set(x - 45 * k, y + 38 * k, z + 60 * k);
  } else {
    controls.target.set(0, 0, -20);
    camera.position.set(-520, 480, 620);
  }
}
goTo(location.hash.slice(1));

addEventListener("keydown", e => {
  if (e.key === "1") goTo("general");
  if (e.key === "2") goTo("main");
  if (e.key === "3") goTo("temple");
  if (e.key === "r" || e.key === "R") controls.autoRotate = !controls.autoRotate;
  if (e.key === "p" || e.key === "P") { pixelArt = !pixelArt; setPixelSize(pixelSize); }
  if (e.key === "o" || e.key === "O") wavesOn = !wavesOn;
  if (e.key === "[") setPixelSize(pixelSize - 1);
  if (e.key === "]") setPixelSize(pixelSize + 1);
});
addEventListener("resize", () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
  composer.setSize(innerWidth, innerHeight);
});
if (params.has("px")) setPixelSize(+params.get("px"));

// Orbit recording (used by record_orbit.py): one full 360 turn, waves advancing between frames.
let recording = params.has("record");
renderer.setAnimationLoop(() => {
  if (recording) return;
  controls.update();
  stepWaves();
  if (pixelArt) composer.render(); else renderer.render(scene, camera);
});
if (recording) {
  await modelReady;
  const frames = +params.get("record") || 360;
  const stepsPerFrame = Math.round(60 / (+params.get("fps") || 15));
  const target = new THREE.Vector3(0, 10, -20);
  const radius = +params.get("radius") || 550, height = +params.get("height") || 115;
  for (let f = 0; f < frames; f++) {
    const a = f / frames * Math.PI * 2;
    camera.position.set(target.x + Math.sin(a) * radius, height, target.z + Math.cos(a) * radius);
    camera.lookAt(target);
    for (let k = 0; k < stepsPerFrame; k++) { updateSwell(); maybeDrop(); simStep(); }
    waterMat.uniforms.tSim.value = stateA.texture;
    composer.render();
    const png = await (await fetch(renderer.domElement.toDataURL("image/png"))).blob();
    await fetch("/frame/" + f, { method: "POST", body: png });
  }
  await fetch("/done", { method: "POST" });
}
</script>
</body>
</html>
"""

with open(out_path, "w") as f:
    f.write(
        html.replace("__VIEWS__", json.dumps(views))
        .replace("__SIM__", json.dumps(sim))
        .replace("__BED__", bed_b64)
        .replace("__GLB__", glb_b64)
    )
print(f"wrote {out_path} ({len(html) + len(glb_b64)} bytes)")
