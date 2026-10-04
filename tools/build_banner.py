#!/usr/bin/env python3
"""Author our textured cube and its native CGFX rotation, using pinned pycgfx.

Requires Python 3.12, Pillow, gltflib, and a checkout of skyfloogle/pycgfx.
The original supplied artwork is represented by assets/icon-art.png; no stock
Nintendo model, texture, animation or audio is used.
"""
import argparse
import base64
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import wave

from PIL import Image


def cube_gltf(texture):
    h = 5.0
    faces = [
        ((0, 0, 1), [(-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h)]),
        ((0, 0, -1), [(h, -h, -h), (-h, -h, -h), (-h, h, -h), (h, h, -h)]),
        ((1, 0, 0), [(h, -h, h), (h, -h, -h), (h, h, -h), (h, h, h)]),
        ((-1, 0, 0), [(-h, -h, -h), (-h, -h, h), (-h, h, h), (-h, h, -h)]),
        ((0, 1, 0), [(-h, h, h), (h, h, h), (h, h, -h), (-h, h, -h)]),
        ((0, -1, 0), [(-h, -h, -h), (h, -h, -h), (h, -h, h), (-h, -h, h)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, vertices in faces:
        offset = len(positions)
        positions.extend(vertices)
        normals.extend([normal] * 4)
        uvs.extend([(0, 1), (1, 1), (1, 0), (0, 0)])
        indices.extend(offset + i for i in [0, 1, 2, 0, 2, 3])
    data = bytearray()
    views, accessors = [], []

    def accessor(values, fmt, kind, component, target, bounds=None):
        while len(data) % 4:
            data.append(0)
        start = len(data)
        for value in values:
            data.extend(struct.pack("<" + fmt, *(value if isinstance(value, tuple) else (value,))))
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(data) - start, "target": target})
        a = {"bufferView": len(views) - 1, "componentType": component, "count": len(values), "type": kind}
        if bounds:
            a["min"], a["max"] = bounds
        accessors.append(a)
        return len(accessors) - 1

    p = accessor(positions, "fff", "VEC3", 5126, 34962, ([-h] * 3, [h] * 3))
    n = accessor(normals, "fff", "VEC3", 5126, 34962)
    uv = accessor(uvs, "ff", "VEC2", 5126, 34962)
    idx = accessor(indices, "H", "SCALAR", 5123, 34963)
    return {
        "asset": {"version": "2.0", "generator": "Re-fract cube banner"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "world", "translation": [0, 1, 0], "rotation": [math.sin(.15), 0, 0, math.cos(.15)], "children": [1]},
            {"name": "spin", "children": [2]},
            {"name": "Re-fract cube", "mesh": 0},
        ],
        "meshes": [{"name": "Icon cube", "primitives": [{"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": uv}, "indices": idx, "material": 0}]}],
        "materials": [{"name": "User artwork", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1}, "doubleSided": False}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"name": "Re-fract icon", "uri": "data:image/png;base64," + base64.b64encode(texture).decode()}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
        "bufferViews": views, "accessors": accessors,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("build/banner"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = root / "assets/icon-art.png"
    art = Image.open(source).convert("RGB")
    if art.size != (256, 256):
        raise ValueError("Banner artwork must be 256x256")
    gltf_file = out / "cube.gltf"
    gltf_file.write_text(json.dumps(cube_gltf(source.read_bytes())), encoding="utf-8")
    converter = args.converter.resolve()
    sys.path.insert(0, str(converter))
    spec = importlib.util.spec_from_file_location("refract_pycgfx", converter / "main.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    import gltflib
    from cgfx.canm import CANM, CANMBoneTransform, FloatAnimationCurve, FloatSegment, InterpolationType, QuantizationType, RepeatMethod, StepLinear64Key

    cgfx = module.convert_gltf(gltflib.GLTF.load(str(gltf_file), load_file_resources=True))
    # Encode Euler yaw directly. Quaternion-to-Euler conversion can introduce
    # a gimbal discontinuity halfway through a full turn.
    animation = CANM()
    animation.name = "COMMON"
    animation.target_animation_group_name = "SkeletalAnimation"
    animation.looping = True
    animation.frame_size = 480.0  # Eight seconds at the banner's 60Hz timeline.
    bone = CANMBoneTransform()
    bone.bone_path = "spin"
    bone.rot_x = bone.rot_z = 0.0
    curve = FloatAnimationCurve()
    curve.start_frame, curve.end_frame = 0.0, 480.0
    curve.pre_repeat_method = curve.post_repeat_method = RepeatMethod.Repeat
    segment = FloatSegment()
    segment.start_frame, segment.end_frame = 0.0, 480.0
    segment.interpolation = InterpolationType.Linear
    segment.quantization = QuantizationType.StepLinear64
    segment.keys = [StepLinear64Key(0, .35), StepLinear64Key(480, .35 + 2 * math.pi)]
    curve.segments.append(segment)
    bone.rot_y = curve
    animation.member_animations_data.add(bone.bone_path, bone)
    cgfx.data.skeletal_animations.add(animation.name, animation)
    encoded = module.write(cgfx)
    if not encoded.startswith(b"CGFX") or len(encoded) > 0x80000:
        raise ValueError("Invalid or oversized HOME Menu CGFX")
    (out / "cube.cgfx").write_bytes(encoded)
    # Required CWAV source, intentionally silent rather than inventing a jingle.
    with wave.open(str(out / "silence.wav"), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(22050)
        audio.writeframes(b"\0\0" * 2205)
    manifest = {"vertices": 24, "triangles": 12, "texture": [256, 256], "animation": "Y rotation", "frames": 480, "looping": True, "cgfx_bytes": len(encoded)}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
