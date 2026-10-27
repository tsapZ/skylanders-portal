# dumps.py (just rd dumps <capture> <out>): every compute dispatch in the frame that reads a render target (the
# render target cache's EDRAM dumps before a resolve), whether that target was drawn to or cleared
# since its previous dump (if not, the dump copies what the EDRAM buffer already holds, unless another
# target's dump overwrote the range in between), and each one's GPU time. RD_ARGS: unused.
import os
import sys

sys.path.insert(0, os.environ["RD_DIR"])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def walk(actions):
    for a in actions:
        yield a
        yield from walk(a.children)


def main(controller):
    textures = {t.resourceId: t for t in controller.GetTextures()}
    rt_ids = {rid for rid, t in textures.items()
              if t.creationFlags & (rd.TextureCategory.ColorTarget | rd.TextureCategory.DepthTarget)}
    durations = {}
    counters = controller.EnumerateCounters()
    if rd.GPUCounter.EventGPUDuration in counters:
        for r in controller.FetchCounters([rd.GPUCounter.EventGPUDuration]):
            durations[r.eventId] = r.value.d
    actions = list(walk(controller.GetRootActions()))
    dirty = {}
    last_dump_eid = {}
    n_dump = n_clean = n_transfer = 0
    t_dump = t_clean = t_draw = t_all = t_transfer = 0.0
    transfer_by_size = {}
    for a in actions:
        d = durations.get(a.eventId, 0.0)
        t_all += d
        if a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Clear):
            controller.SetFrameEvent(a.eventId, True)
            state = controller.GetPipelineState()
            outs = [o.resource for o in state.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
            for o in outs:
                dirty[o] = True
            ds = state.GetDepthTarget()
            if ds.resource != rd.ResourceId.Null():
                dirty[ds.resource] = True
            if a.flags & rd.ActionFlags.Drawcall:
                t_draw += d
                # A draw that reads a host render target is the render target cache's ownership
                # transfer (game draws read textures resolved from the targets, not the targets).
                try:
                    ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel, False)
                except TypeError:
                    ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel)
                if any(ud.descriptor.resource in rt_ids for ud in ros):
                    n_transfer += 1
                    t_transfer += d
                    tgt = outs[0] if outs else ds.resource
                    t = textures.get(tgt)
                    key = "%dx%d" % (t.width, t.height) if t else "?"
                    transfer_by_size[key] = transfer_by_size.get(key, 0.0) + d
            continue
        if not (a.flags & rd.ActionFlags.Dispatch):
            continue
        controller.SetFrameEvent(a.eventId, True)
        state = controller.GetPipelineState()
        try:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Compute, False)
        except TypeError:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Compute)
        srcs = sorted({ud.descriptor.resource for ud in ros if ud.descriptor.resource in rt_ids})
        if not srcs:
            continue
        n_dump += 1
        t_dump += d
        clean = all(not dirty.get(s, True) for s in srcs)
        if clean:
            n_clean += 1
            t_clean += d
        names = ["%s %dx%d" % (str(s), textures[s].width, textures[s].height) for s in srcs]
        log("dispatch", a.eventId, "%.3f ms" % (d * 1000), "clean" if clean else "dirty",
            "prev dump", ",".join(str(last_dump_eid.get(s, "-")) for s in srcs), "|", "; ".join(names))
        for s in srcs:
            dirty[s] = False
            last_dump_eid[s] = a.eventId
    log("transfers by target size:", ", ".join("%s %.2f ms" % (k, v * 1000) for k, v in
                                                sorted(transfer_by_size.items(), key=lambda kv: -kv[1])))
    log("summary: frame %.2f ms, draws %.2f ms (of them %d ownership transfers %.2f ms), dumps %d = "
        "%.2f ms, of them without a draw since the last dump of their target %d = %.2f ms" % (
            t_all * 1000, t_draw * 1000, n_transfer, t_transfer * 1000, n_dump, t_dump * 1000,
            n_clean, t_clean * 1000))


run(main)
