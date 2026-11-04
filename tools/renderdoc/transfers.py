# transfers.py (just rd transfers <capture> <out>): every EDRAM ownership transfer in the frame (a draw that
# reads a host render target and writes another one), timed, with its source and destination and what
# the frame does with the destination next: cleared, overwritten by a full-target draw (no blending, all
# channels, for depth: depth writes with the test always passing), drawn into in a way that keeps what was
# there (needed), read by a dump or another transfer (needed). The time per outcome bounds what skipping
# overwritten transfers could save. RD_ARGS: unused.
import collections
import os
import sys

sys.path.insert(0, os.environ["RD_DIR"])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def walk(actions):
    for a in actions:
        yield a
        yield from walk(a.children)


def describe(textures, rid):
    t = textures.get(rid)
    if t is None:
        return "?"
    return "%dx%d %s x%d" % (t.width, t.height, t.format.Name(), t.msSamp)


def main(controller):
    textures = {t.resourceId: t for t in controller.GetTextures()}
    rt_ids = {rid for rid, t in textures.items()
              if t.creationFlags & (rd.TextureCategory.ColorTarget | rd.TextureCategory.DepthTarget)}
    durations = {}
    if rd.GPUCounter.EventGPUDuration in controller.EnumerateCounters():
        for r in controller.FetchCounters([rd.GPUCounter.EventGPUDuration]):
            durations[r.eventId] = r.value.d
    actions = [a for a in walk(controller.GetRootActions())
               if a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Clear | rd.ActionFlags.Dispatch)]

    info = []
    for a in actions:
        controller.SetFrameEvent(a.eventId, True)
        state = controller.GetPipelineState()
        rec = {"eid": a.eventId, "dur": durations.get(a.eventId, 0.0), "kind": "draw", "reads": set(),
               "writes": set(), "overwrites": set(), "shape": ""}
        if a.flags & rd.ActionFlags.Clear:
            rec["kind"] = "clear"
        elif a.flags & rd.ActionFlags.Dispatch:
            rec["kind"] = "dispatch"
        stage = rd.ShaderStage.Compute if rec["kind"] == "dispatch" else rd.ShaderStage.Pixel
        try:
            ros = state.GetReadOnlyResources(stage, False)
        except TypeError:
            ros = state.GetReadOnlyResources(stage)
        rec["srv_reads"] = {ud.descriptor.resource for ud in ros if ud.descriptor.resource in rt_ids}
        rec["reads"] = set(rec["srv_reads"])
        if rec["kind"] == "clear":
            outs = [o.resource for o in state.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
            ds = state.GetDepthTarget().resource
            rec["writes"] = set(outs) | ({ds} if ds != rd.ResourceId.Null() else set())
            rec["overwrites"] = set(rec["writes"])
        elif rec["kind"] == "draw":
            outs = [o.resource for o in state.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
            ds = state.GetDepthTarget().resource
            d3d = controller.GetD3D12PipelineState()
            dss = d3d.outputMerger.depthStencilState
            depth_writes = ds != rd.ResourceId.Null() and dss.depthEnable and dss.depthWrites
            depth_reads = ds != rd.ResourceId.Null() and (
                (dss.depthEnable and dss.depthFunction != rd.CompareFunction.AlwaysTrue) or dss.stencilEnable)
            blends = list(state.GetColorBlends())
            vp = state.GetViewport(0)
            sc = state.GetScissor(0)
            rec["writes"] = set(outs) | ({ds} if (depth_writes or dss.stencilEnable) else set())
            if depth_reads:
                rec["reads"].add(ds)
            # A full-target draw: one quad or triangle pair, a viewport and scissor covering the target.
            ref = textures.get(outs[0] if outs else ds)
            covers = (ref is not None and min(vp.width, sc.width if sc.enabled else vp.width) >= ref.width * 0.95
                      and min(vp.height, sc.height if sc.enabled else vp.height) >= ref.height * 0.95)
            simple = a.numIndices <= 6 and a.numInstances <= 1
            if covers and simple:
                for i, o in enumerate(outs):
                    b = blends[i] if i < len(blends) else None
                    if b is not None and not b.enabled and (b.writeMask & 0xF) == 0xF:
                        rec["overwrites"].add(o)
                if depth_writes and dss.depthFunction == rd.CompareFunction.AlwaysTrue and not dss.stencilEnable:
                    rec["overwrites"].add(ds)
            rec["shape"] = "%d idx x%d inst, viewport %dx%d, scissor %s" % (
                a.numIndices, a.numInstances, vp.width, vp.height,
                "%dx%d" % (sc.width, sc.height) if sc.enabled else "off")
        # A transfer reads a host render target as a shader resource (game draws read guest textures).
        rec["transfer"] = (rec["kind"] == "draw" and bool(rec["srv_reads"] - rec["writes"]) and
                           bool(rec["writes"]))
        info.append(rec)

    outcome_time = collections.defaultdict(float)
    outcome_count = collections.Counter()
    by_pair = collections.defaultdict(float)
    for i, rec in enumerate(info):
        if not rec["transfer"]:
            continue
        dests = rec["writes"]
        fate = "unused in the frame"
        nxt_desc = ""
        for nxt in info[i + 1:]:
            if nxt["transfer"] and nxt["writes"] & dests and not (nxt["reads"] & dests):
                continue  # the same transfer batch going on
            if nxt["reads"] & dests:
                fate = "read next (dump, transfer, blending or depth test)"
            elif nxt["writes"] & dests:
                if dests <= nxt["overwrites"]:
                    fate = "overwritten whole next (%s)" % nxt["kind"]
                else:
                    fate = "drawn into next, partly or keeping it"
            else:
                continue
            nxt_desc = "next eid %d %s %s" % (nxt["eid"], nxt["kind"], nxt["shape"])
            break
        outcome_time[fate] += rec["dur"]
        outcome_count[fate] += 1
        src = ", ".join(describe(textures, r) for r in sorted(rec["srv_reads"] - rec["writes"]))
        dst = ", ".join(describe(textures, r) for r in sorted(dests))
        by_pair["%s -> %s" % (src, dst)] += rec["dur"]
        log("transfer", rec["eid"], "%.3f ms" % (rec["dur"] * 1000), "|", src, "->", dst, "|", fate, "|",
            nxt_desc)
    log("by source -> destination kind:")
    for k, v in sorted(by_pair.items(), key=lambda kv: -kv[1]):
        log("  %.2f ms  %s" % (v * 1000, k))
    log("by what comes next:")
    for k, v in sorted(outcome_time.items(), key=lambda kv: -kv[1]):
        log("  %.2f ms  %d transfers  %s" % (v * 1000, outcome_count[k], k))
    log("frame %.2f ms" % (sum(r["dur"] for r in info) * 1000))


run(main)
