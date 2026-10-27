# vpos_passes.py (just rd vpos_passes <capture> <out> "<outdir> [min width]"): every game draw that covers a large viewport with a vertex
# shader putting out only the position (so its pixel shader works from the pixel position): its
# input textures of the viewport's size and its output target after the draw, saved as PNG, and a
# line per draw (event, viewport, textures), to compare each output with its inputs (found the
# resolution-scale offsets behind the ghost outlines, see draw_resolution_scaled_pixel_position_centered).
import os
import sys

sys.path.insert(0, os.environ["RD_DIR"])
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


def walk(actions):
    for a in actions:
        yield a
        yield from walk(a.children)


def main(controller):
    args = os.environ.get("RD_ARGS", "").split()
    outdir = args[0]
    min_w = float(args[1]) if len(args) > 1 else 1000
    os.makedirs(outdir, exist_ok=True)
    textures = {t.resourceId: t for t in controller.GetTextures()}
    draws = [a for a in walk(controller.GetRootActions()) if a.flags & rd.ActionFlags.Drawcall]
    log("draws", len(draws))
    # The frame as presented (the last draw's target), to see what was captured.
    if draws:
        controller.SetFrameEvent(draws[-1].eventId, True)
        targets = [o.resource for o in controller.GetPipelineState().GetOutputTargets()
                   if o.resource != rd.ResourceId.Null()]
        if targets and targets[0] in textures:
            save(controller, targets[0], textures[targets[0]], "%s/frame.png" % outdir)
    for a in walk(controller.GetRootActions()):
        if not (a.flags & rd.ActionFlags.Drawcall) or (a.flags & rd.ActionFlags.Indexed):
            continue
        controller.SetFrameEvent(a.eventId, True)
        state = controller.GetPipelineState()
        try:
            vp = state.GetViewport(0)
        except Exception:  # noqa: BLE001
            continue
        if vp.width < min_w or vp.width > 8192:
            continue
        vs = state.GetShaderReflection(rd.ShaderStage.Vertex)
        if not vs:
            continue
        outs = [s for s in vs.outputSignature if s.systemValue != rd.ShaderBuiltin.Position]
        if outs:
            continue  # it hands texture coordinates on: not a pixel position pass
        try:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        except TypeError:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel)
        ins = []
        sizes = []
        for i, ud in enumerate(ros):
            rid = ud.descriptor.resource
            t = textures.get(rid)
            if t:
                sizes.append("%dx%d" % (t.width, t.height))
            if t and abs(t.width - vp.width) < 1 and abs(t.height - vp.height) < 1:
                name = "%s/e%05d_in%02d.png" % (outdir, a.eventId, i)
                if save(controller, rid, t, name):
                    ins.append(name)
        # The pixel shader's code, to see how it turns the position into texture coordinates.
        ps = state.GetShaderReflection(rd.ShaderStage.Pixel)
        if ps:
            try:
                dis = controller.DisassembleShader(state.GetGraphicsPipelineObject(), ps,
                                                   controller.GetDisassemblyTargets(True)[0])
                with open("%s/e%05d_ps.txt" % (outdir, a.eventId), "w", encoding="utf-8") as f:
                    f.write(dis)
            except Exception as e:  # noqa: BLE001
                log("disasm failed", a.eventId, e)
        targets = [o.resource for o in state.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
        out_name = ""
        if targets and ins:
            t = textures.get(targets[0])
            out_name = "%s/e%05d_out.png" % (outdir, a.eventId)
            if not (t and save(controller, targets[0], t, out_name)):
                out_name = ""
        log("event", a.eventId, "viewport %dx%d" % (vp.width, vp.height), "inputs", len(ins), "out", out_name or "-",
            "textures", " ".join(sizes) or "-")


run(main)
