# For each listed event: the pixel shader's constant blocks (reflection name and size, the bound
# range) and the first floats of each, to see which values a draw really uses.
# RD_ARGS: "<eid>[,<eid>...] [<floats per block, default 32>]"
import os
import struct
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    eids = [int(e) for e in args[0].split(",")]
    count = int(args[1]) if len(args) > 1 else 32
    textures = {t.resourceId: t for t in controller.GetTextures()}
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        state = controller.GetPipelineState()
        refl = state.GetShaderReflection(rd.ShaderStage.Pixel)
        log("=== event", eid)
        if refl is None:
            log("  no pixel shader")
            continue
        outs = []
        for o in state.GetOutputTargets():
            t = textures.get(o.resource)
            if t:
                outs.append("%d:%dx%d" % (int(o.resource), t.width, t.height))
        log("  outputs", outs)
        try:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        except TypeError:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel)
        for i, ud in enumerate(ros):
            t = textures.get(ud.descriptor.resource)
            if t:
                log("  srv", i, "%d:%dx%d %s" % (int(ud.descriptor.resource), t.width, t.height,
                                                 t.format.Name()))
        for slot, block in enumerate(refl.constantBlocks):
            desc = state.GetConstantBlock(rd.ShaderStage.Pixel, slot, 0).descriptor
            log("  block", slot, block.name, "refl size", block.byteSize, "bound offset",
                desc.byteOffset, "size", desc.byteSize)
            if desc.resource == rd.ResourceId.Null():
                continue
            size = min(desc.byteSize, count * 4) if desc.byteSize else count * 4
            data = controller.GetBufferData(desc.resource, desc.byteOffset, size)
            n = len(data) // 4
            values = struct.unpack("<%df" % n, data[:n * 4])
            for row in range(0, n, 4):
                log("    c%-3d %s" % (row // 4, " ".join("%12.7g" % v for v in values[row:row + 4])))


run(main)
