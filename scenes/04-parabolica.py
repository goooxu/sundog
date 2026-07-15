#!/usr/bin/env python3
"""sundog scene 04-parabolica — Night-time parabolic spotlight.

A gold parabolic dish — reflective only on its concave back face — focuses
the glow of an emissive bulb sitting at its focus into a light beam that
sweeps across a dark checkered floor, with Spot the cow standing nearby.
A small table lamp (dark shade outside, warm inside, bulb hidden from NEE)
adds a secondary pool of light. The scene showcases the parabola
primitive's custom intersection and two-sided (material_back) semantics.

Run: python3 scenes/04-parabolica.py
"""

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,
                      scale, static_body, translate)

s = Scene()
s.render(width=1920, height=1080, spp=320, max_depth=24, clamp=30, seed=3, exposure=0.25)
s.camera(lookfrom=[3.5, 3.0, 8.0], lookat=[-0.3, 1.1, -0.3], vfov=46, aperture=0.0)
s.background_gradient(horizon=[0.03, 0.035, 0.06], zenith=[0.0, 0.0, 0.008])  # near-black night sky

# ---- textures & meshes ----
s.texture('floor', 'checker', a=[0.28, 0.28, 0.3], b=[0.08, 0.08, 0.09], scale=[16, 16])
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', texture='floor')
s.metal('golddish', color=[1.0, 0.76, 0.33], roughness=0.06)  # gold mirror coating of the dish
s.emissive('beambulb', color=[1.0, 0.85, 0.6], intensity=40.0, two_sided=False)  # bulb at the dish focus
s.lambert('shadeout', color=[0.05, 0.05, 0.06])  # lamp shade, dark exterior
s.lambert('shadein', color=[0.55, 0.3, 0.12])    # lamp shade, warm interior
s.emissive('lampglow', color=[1.0, 0.55, 0.25], intensity=16.0)  # lamp bulb
s.lambert('spotMat', texture='spotSkin')

# ---- lights ----
s.point_light(position=[3.2, 2.2, -4.6], intensity=[120, 82, 45], radius=0.16)  # warm off-camera fill
s.distant_light(direction=[0.3, -1, -0.4], radiance=[0.015, 0.02, 0.035])       # faint blue moonlight

# ---- geometry ----
s.add('rect', 'ground', scale(14))  # checkered floor
s.add('mesh:spot', 'spotMat', scale(0.85), rotate_y(-155), translate(0.9, 0.627, 0.4))  # Spot the cow
s.add('parabola', None, scale(1.6), rotate_z(-115), translate(-3.6, 2.1, 0), material_back='golddish')  # gold dish, concave side only
s.add('sphere', 'beambulb', scale(0.32), translate(-2.875, 1.762, 0))  # emitter at the dish focus
s.add('cylinder', 'shadeout', scale(0.55, 0.75, 0.55), translate(-2.2, 0.8, 3.8), material_back='shadein')  # table-lamp shade
s.add('sphere', 'lampglow', scale(0.34), translate(-2.2, 0.47, 3.8), nee=False)  # bulb under the shade, no NEE

if __name__ == "__main__":
    s.run(out="04-parabolica.png")
