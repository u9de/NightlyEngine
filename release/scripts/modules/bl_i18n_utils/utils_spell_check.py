# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

import enchant
import os
import pickle
import re


class SpellChecker:
    """
    A basic spell checker.
    """

    # These must be all lower case for comparisons
    uimsgs = {
        # OK words
        "adaptively", "adaptivity",
        "aren",  # aren't
        "betweens",  # yuck! in-betweens!
        "boolean", "booleans",
        "chamfer",
        "couldn",  # couldn't
        "customizable",
        "decrement",
        "derivate",
        "deterministically",
        "doesn",  # doesn't
        "duplications",
        "effector",
        "equi",  # equi-angular, etc.
        "fader",
        "globbing",
        "hasn",  # hasn't
        "hetero",
        "hoc",  # ad-hoc
        "incompressible",
        "indices",
        "instantiation",
        "iridas",
        "isn",  # isn't
        "iterable",
        "kyrgyz",
        "latin",
        "merchantability",
        "mplayer",
        "ons",  # add-ons
        "pong",  # ping pong
        "scalable",
        "shadeless",
        "shouldn",  # shouldn't
        "smoothen",
        "spacings",
        "teleport", "teleporting",
        "vertices",
        "wasn",  # wasn't

        # Merged words
        "antialiasing", "antialias",
        "arcsine", "arccosine", "arctangent",
        "autoclip",
        "autocomplete",
        "autoexec",
        "autoexecution",
        "autogenerated",
        "autolock",
        "automask", "automasking",
        "automerge",
        "autoname",
        "autopack",
        "autosave",
        "autoscale",
        "autosmooth",
        "autosplit",
        "backface", "backfacing",
        "backimage",
        "backscattered",
        "bandnoise",
        "bindcode",
        "bitdepth",
        "bitflag", "bitflags",
        "bitrate",
        "blackbody",
        "blendfile",
        "blendin",
        "bonesize",
        "boundbox",
        "boxpack",
        "buffersize",
        "builtin", "builtins",
        "bytecode",
        "chunksize",
        "customdata",
        "dataset", "datasets",
        "de",
        "deadzone",
        "deconstruct",
        "defocus",
        "denoise", "denoised", "denoising", "denoiser",
        "deselect", "deselecting", "deselection",
        "despill", "despilling",
        "dirtree",
        "editcurve",
        "editmesh",
        "filebrowser",
        "filelist",
        "filename", "filenames",
        "filepath", "filepaths",
        "forcefield", "forcefields",
        "fulldome", "fulldomes",
        "fullscreen",
        "gridline", "gridlines",
        "hardlight",
        "hemi",
        "hostname",
        "inbetween",
        "inscatter", "inscattering",
        "libdata",
        "lightcache",
        "lightprobe", "lightprobes",
        "lightless",
        "lineset",
        "linestyle", "linestyles",
        "localview",
        "lookup", "lookups",
        "mathutils",
        "micropolygon",
        "midlevel",
        "midground",
        "mixdown",
        "monospaced",
        "multi",
        "multifractal",
        "multiframe",
        "multilayer",
        "multipaint",
        "multires", "multiresolution",
        "multisampling",
        "multiscatter",
        "multitexture",
        "multithreaded",
        "multiuser",
        "multiview",
        "namespace",
        "nodetree", "nodetrees",
        "keyconfig",
        "offscreen",
        "online",
        "playhead",
        "popup", "popups",
        "pointcloud",
        "pre",
        "precache", "precaching",
        "precalculate",
        "precomputing",
        "prefetch",
        "premultiply", "premultiplied",
        "prepass",
        "prepend",
        "preprocess", "preprocessing", "preprocessor",
        "preseek",
        "promillage",
        "pushdown",
        "raytree",
        "readonly",
        "realtime",
        "reinject", "reinjected",
        "rekey",
        "remesh",
        "reprojection", "reproject", "reprojecting",
        "resize",
        "restpose",
        "resync",
        "retarget", "retargets", "retargeting", "retargeted",
        "retiming",
        "rigidbody",
        "ringnoise",
        "rolloff",
        "runtime",
        "scanline",
        "screenshot", "screenshots",
        "seekability",
        "selfcollision",
        "shadowbuffer", "shadowbuffers",
        "singletexture",
        "spellcheck", "spellchecking",
        "startup",
        "stateful",
        "starfield",
        "studiolight",
        "subflare", "subflares",
        "subframe", "subframes",
        "subclass", "subclasses", "subclassing",
        "subdirectory", "subdirectories", "subdir", "subdirs",
        "subitem",
        "submode",
        "submodule", "submodules",
        "subpath",
        "subsize",
        "substep", "substeps",
        "targetless",
        "textbox", "textboxes",
        "tilemode",
        "timestamp", "timestamps",
        "timestep", "timesteps",
        "todo",
        "tradeoff",
        "un",
        "unassociate", "unassociated",
        "unbake",
        "unclosed",
        "uncomment",
        "unculled",
        "undeformed",
        "undistort", "undistorted", "undistortion",
        "ungroup", "ungrouped",
        "unhide",
        "unindent",
        "unkeyed",
        "unlink", "unlinked",
        "unmute",
        "unphysical",
        "unpremultiply",
        "unprojected",
        "unprotect",
        "unreacted",
        "unreferenced",
        "unregister",
        "unselect", "unselected", "unselectable",
        "unsets",
        "unshadowed",
        "unspill",
        "unstitchable", "unstitch",
        "unsubdivided", "unsubdivide",
        "untrusted",
        "vectorscope",
        "whitespace", "whitespaces",
        "worldspace",
        "workflow",
        "workspace", "workspaces",

        # Neologisms, slangs
        "affectable",
        "animatable",
        "automagic", "automagically",
        "blobby",
        "blockiness", "blocky",
        "collider", "colliders",
        "deformer", "deformers",
        "determinator",
        "editability",
        "effectors",
        "expander",
        "instancer",
        "keyer",
        "lacunarity",
        "linkable",
        "numerics",
        "occluder", "occluders",
        "overridable",
        "passepartout",
        "perspectively",
        "pixelate",
        "pointiness",
        "polycount",
        "polygonization", "polygonalization",  # yuck!
        "scalings",
        "selectable", "selectability",
        "shaper",
        "smoothen", "smoothening",
        "spherize", "spherized",
        "stitchable",
        "symmetrize",
        "trackability",
        "transmissivity",
        "rasterized", "rasterization", "rasterizer",
        "renderer", "renderers", "renderable", "renderability",

        # Really bad!!!
        "convertor",
        "fullscr",

        # Abbreviations
        "aero",
        "amb",
        "anim",
        "aov",
        "app",
        "bbox", "bboxes",
        "bksp",  # Backspace
        "bool",
        "calc",
        "cfl",
        "config", "configs",
        "const",
        "coord", "coords",
        "degr",
        "diff",
        "dof",
        "dupli", "duplis",
        "eg",
        "esc",
        "expr",
        "fac",
        "fra",
        "fract",
        "frs",
        "grless",
        "http",
        "init",
        "irr",  # Irradiance
        "kbit", "kb",
        "lang", "langs",
        "lclick", "rclick",
        "lensdist",
        "loc", "rot", "pos",
        "lorem",
        "luma",
        "mbs",  # mouse button 'select'.
        "mem",
        "multicam",
        "num",
        "ok",
        "orco",
        "ortho",
        "pano",
        "persp",
        "pref", "prefs",
        "prev",
        "param",
        "premul",
        "quad", "quads",
        "quat", "quats",
        "recalc", "recalcs",
        "refl",
        "sce",
        "sel",
        "spec",
        "struct", "structs",
        "subdiv",
        "sys",
        "tex",
        "texcoord",
        "tmr",  # timer
        "tri", "tris",
        "udim", "udims",
        "upres",  # Upresolution
        "usd",
        "uv", "uvs", "uvw", "uw", "uvmap",
        "ve",
        "vec",
        "vel",  # velocity!
        "vert", "verts",
        "vis",
        "vram",
        "xor",
        "xyz", "xzy", "yxz", "yzx", "zxy", "zyx",
        "xy", "xz", "yx", "yz", "zx", "zy",

        # General computer/science terms
        "affine",
        "albedo",
        "anamorphic",
        "anisotropic", "anisotropy",
        "bitangent",
        "boid", "boids",
        "ceil",
        "compressibility",
        "curvilinear",
        "equiangular",
        "equisolid",
        "euler", "eulers",
        "fribidi",
        "gettext",
        "hashable",
        "hotspot",
        "interocular",
        "intrinsics",
        "irradiance",
        "isosurface",
        "jitter", "jittering", "jittered",
        "keymap", "keymaps",
        "lambertian",
        "laplacian",
        "metadata",
        "msgfmt",
        "nand", "xnor",
        "normals",
        "numpad",
        "octahedral",
        "octree",
        "omnidirectional",
        "opengl",
        "openmp",
        "parametrization",
        "photoreceptor",
        "poly",
        "polyline", "polylines",
        "probabilistically",
        "pulldown", "pulldowns",
        "quantized",
        "quartic",
        "quaternion", "quaternions",
        "quintic",
        "samplerate",
        "sawtooth",
        "scrollback",
        "scrollbar",
        "scroller",
        "searchable",
        "spacebar",
        "subtractive",
        "superellipse",
        "tooltip", "tooltips",
        "trackpad",
        "tuple",
        "unicode",
        "viewport", "viewports",
        "viscoelastic",
        "vorticity",
        "waveform", "waveforms",
        "wildcard", "wildcards",
        "wintab",  # Some Windows tablet API

        # General computer graphics terms
        "anaglyph",
        "bezier", "beziers",
        "bicubic",
        "bilinear",
        "bindpose",
        "binormal",
        "blackpoint", "whitepoint",
        "blinn",
        "bokeh",
        "catadioptric",
        "centroid",
        "chroma",
        "chrominance",
        "clearcoat",
        "codec", "codecs",
        "collada",
        "compositing",
        "crossfade",
        "cubemap", "cubemaps",
        "cuda",
        "deinterlace",
        "dropoff",
        "duotone",
        "dv",
        "eigenvectors",
        "emissive",
        "equirectangular",
        "filmlike",
        "fisheye",
        "framerate",
        "gimbal",
        "grayscale",
        "icosphere",
        "inpaint",
        "kerning",
        "lightmap",
        "linearlight",
        "lossless", "lossy",
        "luminance",
        "mantaflow",
        "matcap",
        "midtones",
        "mipmap", "mipmaps", "mip",
        "ngon", "ngons",
        "ntsc",
        "nurb", "nurbs",
        "perlin",
        "phong",
        "pinlight",
        "qi",
        "radiosity",
        "raycasting",
        "raytrace", "raytracing", "raytraced",
        "refractions",
        "remesher", "remeshing", "remesh",
        "renderfarm",
        "scanfill",
        "shader", "shaders",
        "shadowmap", "shadowmaps",
        "softlight",
        "specular", "specularity",
        "spillmap",
        "sobel",
        "stereoscopy",
        "texel",
        "timecode",
        "tonemap",
        "toon",
        "transmissive",
        "vividlight",
        "volumetrics",
        "voronoi",
        "voxel", "voxels",
        "vsync",
        "wireframe",
        "zmask",
        "ztransp",

        # Blender terms
        "audaspace",
        "azone",  # action zone
        "backwire",
        "bbone",
        "bendy",  # bones
        "bmesh",
        "breakdowner",
        "bspline",
        "bweight",
        "colorband",
        "datablock", "datablocks",
        "despeckle",
        "depsgraph",
        "dopesheet",
        "dupliface", "duplifaces",
        "dupliframe", "dupliframes",
        "dupliobject", "dupliob",
        "dupligroup",
        "duplivert",
        "dyntopo",
        "editbone",
        "editmode",
        "eevee",
        "fcurve", "fcurves",
        "fedge", "fedges",
        "filmic",
        "fluidsim",
        "freestyle",
        "enum", "enums",
        "gizmogroup",
        "gon", "gons",  # N-Gon(s)
        "gpencil",
        "idcol",
        "keyframe", "keyframes", "keyframing", "keyframed",
        "lookdev",
        "luminocity",
        "mathvis",
        "metaball", "metaballs", "mball",
        "metaelement", "metaelements",
        "metastrip", "metastrips",
        "movieclip",
        "mpoly",
        "mtex",
        "nabla",
        "navmesh",
        "outliner",
        "overscan",
        "paintmap", "paintmaps",
        "polygroup", "polygroups",
        "poselib",
        "pushpull",
        "pyconstraint", "pyconstraints",
        "qe",  # keys...
        "shaderfx", "shaderfxs",
        "shapekey", "shapekeys",
        "shrinkfatten",
        "shrinkwrap",
        "softbody",
        "stucci",
        "subdiv",
        "subtype",
        "sunsky",
        "tessface", "tessfaces",
        "texface",
        "timeline", "timelines",
        "tosphere",
        "uilist",
        "userpref",
        "vcol", "vcols",
        "vgroup", "vgroups",
        "vinterlace",
        "vse",
        "wasd", "wasdqe",  # keys...
        "wetmap", "wetmaps",
        "wpaint",
        "uvwarp",

        # UOC (Ugly Operator Categories)
        "cachefile",
        "paintcurve",
        "ptcache",
        "dpaint",

        # Algorithm/library names
        "ashikhmin",  # Ashikhmin-Shirley
        "arsloe",  # Texel-Marsen-Arsloe
        "beckmann",
        "blackman",  # Blackman-Harris
        "blosc",
        "burley",  # Christensen-Burley
        "catmull",
        "catrom",
        "chebychev",
        "conrady",  # Brown-Conrady
        "courant",
        "cryptomatte", "crypto",
        "embree",
        "gmp",
        "hosek",
        "kutta",
        "lennard",
        "marsen",  # Texel-Marsen-Arsloe
        "mikktspace",
        "minkowski",
        "minnaert",
        "moskowitz",  # Pierson-Moskowitz
        "musgrave",
        "nayar",
        "netravali",
        "nishita",
        "ogawa",
        "oren",
        "peucker",  # Ramer-Douglas-Peucker
        "pierson",  # Pierson-Moskowitz
        "preetham",
        "prewitt",
        "ramer",  # Ramer-Douglas-Peucker
        "runge",
        "sobol",
        "verlet",
        "wilkie",
        "worley",

        # Acronyms
        "aa", "msaa",
        "ao",
        "aov", "aovs",
        "api",
        "apic",  # Affine Particle-In-Cell
        "asc", "cdl",
        "ascii",
        "atrac",
        "avx",
        "bsdf",
        "bssrdf",
        "bw",
        "ccd",
        "cmd",
        "cmos",
        "cpus",
        "ctrl",
        "cw", "ccw",
        "dev",
        "djv",
        "dpi",
        "dvar",
        "dx",
        "eo",
        "fh",
        "fk",
        "fov",
        "fft",
        "futura",
        "fx",
        "gfx",
        "ggx",
        "gl",
        "glsl",
        "gpl",
        "gpu", "gpus",
        "hc",
        "hdc",
        "hdr", "hdri", "hdris",
        "hh", "mm", "ss", "ff",  # hh:mm:ss:ff timecode
        "hsv", "hsva", "hsl",
        "id",
        "ies",
        "ior",
        "itu",
        "jonswap",
        "lhs",
        "lmb", "mmb", "rmb",
        "kb",
        "mocap",
        "msgid", "msgids",
        "mux",
        "ndof",
        "ppc",
        "precisa",
        "px",
        "qmc",
        "rdp",
        "rgb", "rgba",
        "rhs",
        "rv",
        "sdl",
        "sl",
        "smpte",
        "ssao",
        "ssr",
        "svn",
        "tma",
        "ui",
        "unix",
        "vbo", "vbos",
        "vr",
        "wxyz",
        "xr",
        "ycc", "ycca",
        "yrgb",
        "yuv", "yuva",

        # Blender acronyms
        "bge",
        "bli",
        "bpy",
        "bvh",
        "dbvt",
        "dop",  # BLI K-Dop BVH
        "ik",
        "nla",
        "py",
        "qbvh",
        "rna",
        "rvo",
        "simd",
        "sph",
        "svbvh",

        # Files types/formats
        "avi",
        "attrac",
        "autocad",
        "autodesk",
        "bmp",
        "btx",
        "cineon",
        "dpx",
        "dwaa",
        "dwab",
        "dxf",
        "eps",
        "exr",
        "fbx",
        "fbxnode",
        "ffmpeg",
        "flac",
        "gltf",
        "gzip",
        "ico",
        "jpg", "jpeg", "jpegs",
        "json",
        "matroska",
        "mdd",
        "mkv",
        "mpeg", "mjpeg",
        "mtl",
        "ogg",
        "openjpeg",
        "osl",
        "oso",
        "piz",
        "png", "pngs",
        "po",
        "quicktime",
        "rle",
        "sgi",
        "stl",
        "svg",
        "targa", "tga",
        "tiff",
        "theora",
        "vorbis",
        "vp9",
        "wav",
        "webm",
        "xiph",
        "xml",
        "xna",
        "xvid",
    }

    _valid_before = "(?<=[\\s*'\"`])|(?<=[a-zA-Z][/-])|(?<=^)"
    _valid_after = "(?=[\\s'\"`.!?,;:])|(?=[/-]\\s*[a-zA-Z])|(?=$)"
    _valid_words = "(?:{})(?:(?:[A-Z]+[a-z]*)|[A-Z]*|[a-z]*)(?:{})".format(_valid_before, _valid_after)
    _split_words = re.compile(_valid_words).findall

    @classmethod
    def split_words(cls, text):
        return [w for w in cls._split_words(text) if w]

    def __init__(self, settings, lang="en_US"):
        self.settings = settings
        self.dict_spelling = enchant.Dict(lang)
        self.cache = set(self.uimsgs)

        cache = self.settings.SPELL_CACHE
        if cache and os.path.exists(cache):
            with open(cache, 'rb') as f:
                self.cache |= set(pickle.load(f))

    def __del__(self):
        cache = self.settings.SPELL_CACHE
        if cache and os.path.exists(cache):
            with open(cache, 'wb') as f:
                pickle.dump(self.cache, f)

    def check(self, txt):
        ret = []

        if txt in self.cache:
            return ret

        for w in self.split_words(txt):
            w_lower = w.lower()
            if w_lower in self.cache:
                continue
            if not self.dict_spelling.check(w):
                ret.append((w, self.dict_spelling.suggest(w)))
            else:
                self.cache.add(w_lower)

        if not ret:
            self.cache.add(txt)

        return ret
