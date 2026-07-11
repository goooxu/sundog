# sundog scene format

Scenes are JSON with eight sections: `render`, `camera`, `background`,
`textures`, `materials`, `meshes`, `objects`, `lights`.

## Canonical shapes (object space)

| shape | definition | front face | UV |
|---|---|---|---|
| `sphere` | r=1 at origin | outside | spherical (u: azimuth, v: latitude) |
| `rect` | XZ square [-1,1]^2 at y=0 | +Y side | u=(x+1)/2, v=(z+1)/2 |
| `disk` | XZ disk r<=1 at y=0 | +Y side | u: azimuth, v: radius |
| `cylinder` | x^2+z^2=1, y in [-1,1], **open ends** | outside | u: azimuth, v: height |
| `parabola` | y = (x^2+z^2)/2, r<=1 (focus at (0,0.5,0)) | **convex underside** | u: azimuth, v: radius |
| `mesh:NAME` | triangles from `meshes.NAME` | winding side | OBJ `vt` per-vertex; barycentric fallback without `vt` |

To make a *concave* parabola reflector (dish), put the mirror on
`material_back` (the bowl side) and set `material` to `null`… or rotate the
bowl to aim its convex side away. Remember: `"material"` is the front face.

## Objects

```json
{ "shape": "rect", "material": "gold", "material_back": null,
  "cutout": "logoTex", "nee": true,
  "transform": [ { "scale": [2,1,2] }, { "rotate_x": 90 }, { "translate": [0,2,0] } ] }
```

- `material` (front face). `material_back`: omitted = same as front;
  `null` = **pass-through** (rays go straight through when hitting that side —
  cxxrt semantics; shadows pass through too). `material` may itself be `null`
  when `material_back` is set (back-only surface).
- `cutout`: texture name; alpha < 0.5 makes holes (both faces).
- `transform`: list applied top-down in object space — `[scale, rotate, translate]`
  means translate(rotate(scale(p))). Angles in degrees. Steps: `scale` (number
  or [x,y,z]), `rotate_x/y/z`, `translate`, `matrix` (12 floats, row-major 3x4).
  Non-uniform scale is fine (normals handled correctly).
- `nee`: set `false` to keep an emissive object out of explicit light sampling
  (it then only contributes when a path hits it).

## Materials

- `lambert`: `color` or `texture`
- `metal`: GGX, `color` (F0) or `texture`, `roughness` (0 = mirror)
- `dielectric`: `ior` (smooth glass)
- `emissive`: `color`/`texture`, `intensity`, `two_sided` (default false —
  only the front face emits). Emissive `rect`/`disk`/`sphere` objects are
  automatically sampled as area lights (unless `nee: false`). Sphere/disk
  area lights require uniform scale. Textured emissives are supported as NEE
  lights for `rect`/`disk`; a textured emissive `sphere` must use
  `nee: false` (its uv frame is object-space and cannot be light-sampled).

## Textures

- `solid`: `color`
- `checker`: `a`, `b`, `scale: [su, sv]`
- `grid`: `a` (cell), `b` (line), `scale`, `width` (line width, cell fraction)
- `image`: `file` (PNG etc., relative to the scene file), `srgb` (default true)

## Lights (explicit delta lights)

```json
{ "type": "point", "position": [x,y,z], "radius": 0.3, "intensity": [r,g,b] }
{ "type": "distant", "direction": [x,y,z], "radiance": [r,g,b] }
```

`point` has a radius for soft shadows; `intensity` is radiant intensity
(falls off with 1/d^2). `distant` is a parallel light.
Area lights are *not* declared here — make an emissive object instead.

## Background

`{ "type": "solid", "color": [r,g,b] }` or
`{ "type": "gradient", "horizon": [r,g,b], "zenith": [r,g,b] }` (lerp on ray y).

## Render settings

`width height spp max_depth seed clamp gamma exposure` — all overridable from
the CLI. `clamp` limits indirect per-sample contributions (firefly control,
0 = off). Fixed `seed` gives bit-identical images on the same GPU/driver.
