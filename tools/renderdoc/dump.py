# Save the bound color outputs (and optionally depth) after each listed event as PNG.
# RD_ARGS: "<outdir> <eid>[,<eid>...] [depth]"
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    outdir = args[0]
    eids = [int(e) for e in args[1].split(",")]
    with_depth = len(args) > 2 and args[2] == "depth"
    os.makedirs(outdir, exist_ok=True)
    textures = {t.resourceId: t for t in controller.GetTextures()}
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        state = controller.GetPipelineState()
        targets = [t.resource for t in state.GetOutputTargets() if t.resource != rd.ResourceId.Null()]
        if with_depth:
            d = state.GetDepthTarget().resource
            if d != rd.ResourceId.Null():
                targets.append(d)
        for rid in targets:
            t = textures[rid]
            ts = rd.TextureSave()
            ts.resourceId = rid
            ts.destType = rd.FileType.PNG
            ts.alpha = rd.AlphaMapping.Discard
            ts.mip = 0
            ts.slice.sliceIndex = 0
            if t.msSamp > 1:
                ts.sample.sampleIndex = 0
            name = "%s/e%05d_r%d.png" % (outdir, eid, int(rid))
            if t.format.compType == rd.CompType.Depth or "R32" in t.format.Name():
                ts.comp.blackPoint = 0.0
                ts.comp.whitePoint = 1.0
            ok = controller.SaveTexture(ts, name)
            log(eid, int(rid), t.width, t.height, t.format.Name(), "ok" if ok else "FAILED", name)


run(main)
