# Search the pixel shader constants of every draw for given float values, e.g. to find the game's
# FXAA pass by its reciprocal frame size (1/1280, 0.5/1280, ...) and FXAA 3.11's 360 ConstDir
# (1, -1, 0.25, -0.25). Reports the draws with at least RD_ARGS' minimum number of distinct hits.
# RD_ARGS: "<min hits> <value>[,<value>...] [<first event>-<last event>]" (the range keeps it fast:
# every draw needs a replay to its event)
import os
import struct
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    min_hits = int(args[0])
    wanted = [float(v) for v in args[1].split(",")]
    first, last = 0, 1 << 30
    if len(args) > 2:
        first, last = (int(x) for x in args[2].split("-"))
    sdfile = controller.GetStructuredFile()
    textures = {t.resourceId: t for t in controller.GetTextures()}

    def draws(actions):
        for a in actions:
            if a.flags & rd.ActionFlags.Drawcall:
                yield a
            if a.children:
                yield from draws(a.children)

    def close(x, v):
        return abs(x - v) <= max(1e-7, abs(v) * 1e-4)

    found = 0
    for a in draws(controller.GetRootActions()):
        if not first <= a.eventId <= last:
            continue
        controller.SetFrameEvent(a.eventId, False)
        state = controller.GetPipelineState()
        refl = state.GetShaderReflection(rd.ShaderStage.Pixel)
        if refl is None:
            continue
        hits = {}
        for slot in range(len(refl.constantBlocks)):
            try:
                block = state.GetConstantBlock(rd.ShaderStage.Pixel, slot, 0).descriptor
            except Exception:  # noqa: BLE001
                continue
            if block.resource == rd.ResourceId.Null() or block.byteSize == 0:
                continue
            data = controller.GetBufferData(block.resource, block.byteOffset,
                                            min(block.byteSize, 65536))
            count = len(data) // 4
            values = struct.unpack("<%df" % count, data[:count * 4])
            for i, x in enumerate(values):
                for v in wanted:
                    if close(x, v):
                        hits.setdefault(v, []).append((slot, i // 4, "xyzw"[i % 4]))
        if len(hits) >= min_hits:
            found += 1
            outs = []
            for o in a.outputs:
                if o != rd.ResourceId.Null() and o in textures:
                    t = textures[o]
                    outs.append("%dx%d:%s" % (t.width, t.height, t.format.Name()))
            log("%6d %s  out=%s" % (a.eventId, a.GetName(sdfile), ",".join(outs)))
            for v, where in sorted(hits.items()):
                log("    %-12g at %s" % (v, " ".join("b%d.c%d.%s" % w for w in where[:6])))
    log("draws with >= %d of the values: %d" % (min_hits, found))


run(main)
