# For each listed event: print pixel shader resources, blend/depth state, save input textures.
# RD_ARGS: "<outdir> <eid>[,<eid>...]"
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def save(controller, rid, tex, name):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    ts.alpha = rd.AlphaMapping.Discard
    if tex.msSamp > 1:
        ts.sample.sampleIndex = 0
    return controller.SaveTexture(ts, name)


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    outdir = args[0]
    os.makedirs(outdir, exist_ok=True)
    eids = [int(e) for e in args[1].split(",")]
    textures = {t.resourceId: t for t in controller.GetTextures()}
    buffers = {b.resourceId: b for b in controller.GetBuffers()}
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        state = controller.GetPipelineState()
        log("=== event", eid)
        ps = state.GetShaderReflection(rd.ShaderStage.Pixel)
        log("PS:", int(state.GetShader(rd.ShaderStage.Pixel)), "entry", state.GetShaderEntryPoint(rd.ShaderStage.Pixel))
        try:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        except TypeError:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel)
        for i, ud in enumerate(ros):
            d = ud.descriptor
            rid = d.resource
            if rid in textures:
                t = textures[rid]
                name = "%s/e%05d_ps%02d_r%d.png" % (outdir, eid, i, int(rid))
                ok = save(controller, rid, t, name)
                log("  srv", i, int(rid), "tex %dx%d %s" % (t.width, t.height, t.format.Name()),
                    "view fmt", d.format.Name(), "saved" if ok else "save failed")
            elif rid in buffers:
                log("  srv", i, int(rid), "buffer", buffers[rid].length)
            else:
                log("  srv", i, int(rid), "?")
        cb = state.GetColorBlends()
        for i, b in enumerate(cb[:4]):
            log("  blend", i, "enabled", b.enabled, "color", b.colorBlend.source, b.colorBlend.operation,
                b.colorBlend.destination, "alpha", b.alphaBlend.source, b.alphaBlend.operation,
                b.alphaBlend.destination, "mask", b.writeMask)
        ds = state.GetDepthState() if hasattr(state, "GetDepthState") else None
        if ds is not None:
            log("  depth test", ds.depthEnable, "write", ds.depthWrites, "func", ds.depthFunction)
        ss = state.GetStencilFaces() if hasattr(state, "GetStencilFaces") else None
        if ss:
            log("  stencil front", ss[0].function, "pass", ss[0].passOperation, "ref", ss[0].reference,
                "cmp mask", ss[0].compareMask)
        try:
            vp = state.GetViewport(0)
            sc = state.GetScissor(0)
            log("  viewport", vp.x, vp.y, vp.width, vp.height, "scissor", sc.x, sc.y, sc.width, sc.height)
        except Exception as ex:  # noqa: BLE001
            log("  viewport n/a", ex)
        log("  outputs", [int(t.resource) for t in state.GetOutputTargets()], "depth",
            int(state.GetDepthTarget().resource))


run(main)
