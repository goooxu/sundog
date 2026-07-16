/* sundog_api.h — public C ABI of libsundog.so.
 *
 * The scene flows from Python (scenes/scenelib.py) into the renderer through
 * these calls — no intermediate representation, no files, no subprocess.
 * Scalars cross as double and are narrowed to float immediately at the API
 * boundary (the same single IEEE narrowing the old JSON loader performed at
 * its leaves); "not set, renderer default applies" is expressed as NaN for
 * doubles, NULL for vectors, -1 for tri-state ints. Renderer defaults live
 * exclusively on the C++ side.
 *
 * Call-order contract (enforced): config/registries -> objects -> flames ->
 * explicit lights -> render. This freezes the light-list layout
 * [auto NEE area lights in object order][2 point lights per flame][explicit]
 * that the device sampling code and the golden images depend on.
 */
#ifndef SUNDOG_API_H
#define SUNDOG_API_H

#include <stdint.h>

#ifdef __cplusplus
#define SUNDOG_API extern "C" __attribute__((visibility("default")))
#else
#define SUNDOG_API __attribute__((visibility("default")))
#endif

/* ---- error model: 0 = ok; nonzero -> sundog_last_error() has the story -- */
#define SUNDOG_OK 0
#define SUNDOG_ERROR 1
SUNDOG_API const char* sundog_last_error(void); /* thread-local message */

/* ---- lifetime ------------------------------------------------------------ */
typedef struct sundog_scene sundog_scene; /* opaque */
SUNDOG_API sundog_scene* sundog_scene_create(const char* base_dir);
SUNDOG_API void sundog_scene_destroy(sundog_scene*);

/* ---- enums (geom values mirror device/params.h GeomKind) ----------------- */
enum {
  SUNDOG_GK_SPHERE = 0,
  SUNDOG_GK_RECT = 1,
  SUNDOG_GK_DISK = 2,
  SUNDOG_GK_CYLINDER = 3,
  SUNDOG_GK_PARABOLA = 4,
  SUNDOG_GK_MESH = 5,
};
enum { SUNDOG_TM_ACES = 0, SUNDOG_TM_CLAMP = 1 };
enum {
  SUNDOG_XF_SCALE = 0,
  SUNDOG_XF_TRANSLATE = 1,
  SUNDOG_XF_ROTATE_X = 2,
  SUNDOG_XF_ROTATE_Y = 3,
  SUNDOG_XF_ROTATE_Z = 4,
};
#define SUNDOG_MAT_NONE (-1)    /* explicit null face: pass-through */
#define SUNDOG_MAT_DEFAULT (-2) /* material_back omitted: same as front */

/* ---- config blocks (phase 0; each at most once) --------------------------- */
SUNDOG_API int sundog_set_render(sundog_scene*, int width, int height, int spp,
                                 int max_depth, double clamp, int64_t seed,
                                 double gamma, double exposure,
                                 int tonemap /* -1|SUNDOG_TM_* */,
                                 int transparent_shadows /* -1|0|1 */);
SUNDOG_API int sundog_set_camera(sundog_scene*, const double lookfrom[3],
                                 const double lookat[3],
                                 const double up[3] /* NULL */, double vfov,
                                 double aperture, double focus_dist);
SUNDOG_API int sundog_set_background_solid(sundog_scene*, const double color[3]);
SUNDOG_API int sundog_set_background_gradient(sundog_scene*,
                                              const double horizon[3],
                                              const double zenith[3]);
SUNDOG_API int sundog_set_background_envmap(sundog_scene*, const char* file,
                                            double rotate_deg, double intensity,
                                            int importance /* -1|0|1 */);
SUNDOG_API int sundog_set_physics(sundog_scene*, const double gravity[3],
                                  double timestep, double max_time,
                                  double friction, double restitution,
                                  int pos_iters /* -1 */, int vel_iters /* -1 */,
                                  double sleep_threshold, double stop_time);

/* ---- registries (phase 0; return id >= 0, or -1 with last_error set) ------ */
SUNDOG_API int sundog_add_texture_solid(sundog_scene*, const double color[3]);
SUNDOG_API int sundog_add_texture_checker(sundog_scene*, const double a[3],
                                          const double b[3],
                                          const double scale[2] /* NULL=(8,8) */);
SUNDOG_API int sundog_add_texture_grid(sundog_scene*, const double a[3],
                                       const double b[3], const double scale[2],
                                       double width /* NaN=0.05 */);
SUNDOG_API int sundog_add_texture_image(sundog_scene*, const char* file,
                                        int srgb /* -1 -> 1 */);

SUNDOG_API int sundog_add_material_lambert(sundog_scene*,
                                           const double color[3] /* NULL */,
                                           int tex_id /* -1 */);
SUNDOG_API int sundog_add_material_metal(sundog_scene*, const double color[3],
                                         int tex_id, double roughness /* NaN */);
SUNDOG_API int sundog_add_material_dielectric(sundog_scene*, double ior /* NaN */,
                                              const double absorb[3] /* NULL */,
                                              const double color[3], int tex_id);
SUNDOG_API int sundog_add_material_emissive(sundog_scene*, const double color[3],
                                            int tex_id, double intensity /* NaN */,
                                            int two_sided /* -1 -> 0 */);
SUNDOG_API int sundog_add_material_water(sundog_scene*, double ior /* NaN=1.33 */,
                                         const double absorb[3] /* NULL */,
                                         double wave_amp /* NaN=0.05 */,
                                         double wave_freq /* NaN=2.0 */,
                                         const double color[3] /* NULL=white */);

SUNDOG_API int sundog_add_mesh(sundog_scene*, const char* obj_file,
                               int smooth_normals /* -1 -> 1 */,
                               const char* usemtl /* NULL = whole mesh */);

/* ---- objects (phase 1) ----------------------------------------------------
 * Transform steps compose in C++ (top-down: later steps wrap earlier ones),
 * bit-identically to the old loader. SCALE/TRANSLATE use (a,b,c); ROTATE_*
 * takes degrees in a. Uniform scale is splat to (s,s,s) by the caller. */
typedef struct {
  int32_t kind; /* SUNDOG_XF_* */
  double a, b, c;
} sundog_xform_step;

typedef struct {
  int32_t dynamic; /* 0|1 */
  double density;  /* NaN = 250 */
  int32_t has_velocity, has_angular_velocity;
  double velocity[3], angular_velocity[3];
  double friction, restitution; /* NaN = inherit scene value */
  double thickness;             /* NaN = 0.2 (rect slab depth) */
} sundog_physics_body;

SUNDOG_API int sundog_add_object(sundog_scene*, int geom_kind,
                                 int mesh_id /* -1 unless GK_MESH */,
                                 int mat_front /* id | SUNDOG_MAT_NONE */,
                                 int mat_back /* id | NONE | DEFAULT */,
                                 int cutout_tex_id /* -1 */,
                                 const sundog_xform_step* steps, int num_steps,
                                 int nee /* -1 -> 1 */,
                                 const sundog_physics_body* physics /* NULL */);

/* ---- flames (phase 2), explicit lights (phase 3) -------------------------- */
SUNDOG_API int sundog_add_flame(sundog_scene*, const double base[3],
                                double height, double radius,
                                double intensity /* NaN=20 */,
                                double sigma /* NaN=4 */,
                                double noise_scale /* NaN=3 */,
                                int64_t seed /* -1 -> 0 */,
                                double light_intensity /* NaN=12 */);
SUNDOG_API int sundog_add_point_light(sundog_scene*, const double position[3],
                                      const double intensity[3],
                                      double radius /* NaN=0 */);
SUNDOG_API int sundog_add_distant_light(sundog_scene*, const double direction[3],
                                        const double radiance[3]);

/* ---- validation / probe / render ------------------------------------------ */
SUNDOG_API int sundog_scene_validate(sundog_scene*); /* GPU-free checks */

typedef struct {
  char name[256];
  int32_t cc_major, cc_minor;
  int32_t driver_version, runtime_version; /* raw CUDA encoding (1000x+10y) */
  uint64_t total_mem_mb;
  uint32_t optix_header_version; /* ABI encoding: 10000*maj+100*min+patch */
  uint32_t rtcore_version;       /* 10*maj+min */
} sundog_device_info;
SUNDOG_API int sundog_probe(sundog_device_info* out);

typedef struct {
  const char* out_path;   /* required */
  const char* stats_path; /* NULL = no stats sidecar */
  const char* aov_albedo_path, *aov_normal_path; /* NULL = skip */
  const char* scene_name; /* label for banner + stats "scene" field */
  int32_t spp, width, height;           /* -1 = scene value */
  int64_t seed;                         /* -1 = scene value */
  int32_t denoise, transparent_shadows; /* -1|0|1 */
  double clamp, gamma;                  /* NaN = scene value */
  int32_t tonemap;                      /* -1|SUNDOG_TM_* */
  double physics_time;                  /* NaN; >=0 overrides stop_time */
  int32_t quiet;                        /* 0|1 */
} sundog_render_options;
SUNDOG_API int sundog_render(sundog_scene*, const sundog_render_options*);

#endif /* SUNDOG_API_H */
