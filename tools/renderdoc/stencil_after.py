# stencil_after.py (just rd stencil_after <capture> <out>): for each EDRAM ownership transfer into a depth
# target whose next use overwrites the depth whole (depth writes, test always passing, stencil off), what the
# frame does with that target's stencil afterwards, up to the next time the target is cleared or written by
# another transfer: a draw with stencil on (the transferred stencil is used), a dump or transfer reading the
# target (the stencil goes on into memory or another target), or nothing (the transfer could be skipped
# without changing the picture). Bounds what a depth-target transfer skip could save. RD_ARGS: unused.
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
    depth_ids = {rid for rid, t in textures.items() if t.creationFlags & rd.TextureCategory.DepthTarget}
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
        rec = {"eid": a.eventId, "dur": durations.get(a.eventId, 0.0), "kind": "draw", "writes": set(),
               "ds": None, "stencil": False, "depth_overwrite": False, "stencil_write_mask": 0,
               "overwrites_color": set()}
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
        outs = [o.resource for o in state.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
        ds = state.GetDepthTarget().resource
        if rec["kind"] == "clear":
            rec["writes"] = set(outs) | ({ds} if ds != rd.ResourceId.Null() else set())
        elif rec["kind"] == "draw":
            dss = controller.GetD3D12PipelineState().outputMerger.depthStencilState
            if ds != rd.ResourceId.Null():
                rec["ds"] = ds
                rec["stencil"] = bool(dss.stencilEnable)
                rec["depth_overwrite"] = bool(dss.depthEnable and dss.depthWrites and
                                              dss.depthFunction == rd.CompareFunction.AlwaysTrue and
                                              not dss.stencilEnable)
                try:
                    rec["stencil_write_mask"] = int(dss.frontFace.writeMask)
                except Exception:  # noqa: BLE001
                    rec["stencil_write_mask"] = -1
            depth_writes = ds != rd.ResourceId.Null() and dss.depthEnable and dss.depthWrites
            rec["writes"] = set(outs) | ({ds} if (depth_writes or dss.stencilEnable) else set())
            # Color targets this draw writes whole: one quad or triangle covering them, no blending.
            blends = list(state.GetColorBlends())
            vp = state.GetViewport(0)
            sc = state.GetScissor(0)
            if a.numIndices <= 6 and a.numInstances <= 1:
                for n, o in enumerate(outs):
                    t = textures.get(o)
                    b = blends[n] if n < len(blends) else None
                    covers = (t is not None and min(vp.width, sc.width if sc.enabled else vp.width) >= t.width * 0.95
                              and min(vp.height, sc.height if sc.enabled else vp.height) >= t.height * 0.95)
                    if covers and b is not None and not b.enabled and (b.writeMask & 0xF) == 0xF:
                        rec["overwrites_color"].add(o)
        rec["transfer"] = (rec["kind"] == "draw" and bool(rec["srv_reads"] - rec["writes"]) and
                           bool(rec["writes"]))
        info.append(rec)

    def follow(target, start, depth):
        """What happens to target's stencil from info[start] on: (fate, detail). A transfer reading it hands
        the stencil to its destination, which is followed in turn (up to 6 hops)."""
        for k in range(start, len(info)):
            nxt = info[k]
            if nxt["kind"] == "clear" and target in nxt["writes"]:
                return "unused: cleared", "cleared at %d" % nxt["eid"]
            if nxt["transfer"] and target in nxt["writes"] and target not in nxt["srv_reads"]:
                return "unused: rewritten by a transfer", "transfer %d" % nxt["eid"]
            if target in nxt["srv_reads"]:
                dest_depth = (nxt["writes"] - {target}) & depth_ids
                if nxt["transfer"] and dest_depth and depth < 6:
                    t2 = next(iter(dest_depth))
                    fate, detail = follow(t2, k + 1, depth + 1)
                    return fate, "-> %d %s; %s" % (nxt["eid"], describe(textures, t2), detail)
                if nxt["transfer"]:
                    # The depth and stencil bits land in a color target: what happens to that one next.
                    color = next(iter(nxt["writes"] - {target}), None)
                    for m in range(k + 1, len(info)):
                        c = info[m]
                        if color is None:
                            break
                        if c["kind"] == "clear" and color in c["writes"]:
                            return "unused: in a color target, then cleared", "-> %d; cleared %d" % (nxt["eid"], c["eid"])
                        if color in c["srv_reads"]:
                            return "in a color target that is read (%s)" % c["kind"], "-> %d; %s %d" % (
                                nxt["eid"], c["kind"], c["eid"])
                        if color in c["overwrites_color"]:
                            return "unused: in a color target, then overwritten whole", "-> %d; draw %d" % (
                                nxt["eid"], c["eid"])
                        if color in c["writes"]:
                            return "in a color target that is drawn into (kept)", "-> %d; draw %d" % (
                                nxt["eid"], c["eid"])
                    return "unused: in a color target until the end of frame", "-> %d" % nxt["eid"]
                return "read by a %s (stencil may go to memory)" % nxt["kind"], "%s %d" % (nxt["kind"], nxt["eid"])
            if nxt["ds"] == target and nxt["stencil"]:
                return "a draw uses the stencil", "draw %d (write mask %d)" % (nxt["eid"], nxt["stencil_write_mask"])
        return "unused: end of frame", "end of frame"

    fate_time = collections.defaultdict(float)
    fate_count = collections.Counter()
    for i, rec in enumerate(info):
        if not rec["transfer"]:
            continue
        dests = rec["writes"] & depth_ids
        if not dests:
            continue
        target = next(iter(dests))
        # The first use after the transfer batch: must be a depth-only overwrite for this to matter.
        first = None
        j = i + 1
        while j < len(info):
            nxt = info[j]
            if nxt["transfer"] and target in nxt["writes"] and target not in nxt["srv_reads"]:
                j += 1
                continue
            if target in nxt["writes"] or target in nxt["srv_reads"] or nxt["ds"] == target:
                first = nxt
                break
            j += 1
        if first is None or not first["depth_overwrite"] or first["ds"] != target:
            continue
        fate, detail = follow(target, j + 1, 0)
        fate_time[fate] += rec["dur"]
        fate_count[fate] += 1
        src = ", ".join(describe(textures, r) for r in sorted(rec["srv_reads"] - rec["writes"]))
        log("transfer", rec["eid"], "%.3f ms" % (rec["dur"] * 1000), "|", src, "->", describe(textures, target),
            "| first", first["eid"], "|", fate, "|", detail)
    log("by fate:")
    for k, v in sorted(fate_time.items(), key=lambda kv: -kv[1]):
        log("  %.2f ms  %d transfers  %s" % (v * 1000, fate_count[k], k))
    log("frame %.2f ms" % (sum(r["dur"] for r in info) * 1000))


run(main)
