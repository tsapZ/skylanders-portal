# For each listed event: the vertex shader's outputs per vertex (positions, texture coordinates),
# e.g. to see which part of a texture a full-screen quad samples.
# RD_ARGS: "<eid>[,<eid>...] [max vertices]"
import os
import struct
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    eids = [int(e) for e in args[0].split(",")]
    limit = int(args[1]) if len(args) > 1 else 8
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        state = controller.GetPipelineState()
        refl = state.GetShaderReflection(rd.ShaderStage.Vertex)
        mesh = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        log("=== event", eid, "vertices", mesh.numIndices, "stride", mesh.vertexByteStride)
        if not refl or mesh.vertexResourceId == rd.ResourceId.Null() or not mesh.vertexByteStride:
            log("  no post-VS data")
            continue
        sig = refl.outputSignature
        count = min(mesh.numIndices, limit)
        data = controller.GetBufferData(mesh.vertexResourceId, mesh.vertexByteOffset,
                                        mesh.vertexByteStride * count)
        for v in range(count):
            base = v * mesh.vertexByteStride
            parts = []
            offset = 0
            for s in sig:
                n = s.compCount
                vals = struct.unpack_from("<%df" % n, data, base + offset)
                parts.append("%s%d=(%s)" % (s.semanticName or s.varName, s.semanticIndex,
                                            ", ".join("%.4f" % x for x in vals)))
                offset += 4 * n
            log("  v%d" % v, " ".join(parts))


run(main)
