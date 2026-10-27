# How much GPU time goes to render target emulation: actions that read a host render target as a
# shader resource are EDRAM ownership transfers (draws) or EDRAM dumps for resolves (dispatches);
# game draws read guest textures from the texture cache instead. Other dispatches are resolves and
# texture loads. Prints the split and the most expensive emulation actions.
import collections
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    sdfile = controller.GetStructuredFile()
    textures = {t.resourceId: t for t in controller.GetTextures()}
    actions = []

    def visit(items):
        for a in items:
            if a.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch):
                actions.append(a)
            if a.children:
                visit(a.children)

    visit(controller.GetRootActions())
    host_rts = set()
    for a in actions:
        for o in a.outputs:
            if o != rd.ResourceId.Null():
                host_rts.add(o)
        if a.depthOut != rd.ResourceId.Null():
            host_rts.add(a.depthOut)
    durations = {r.eventId: r.value.d for r in controller.FetchCounters([rd.GPUCounter.EventGPUDuration])}

    groups = collections.Counter()
    counts = collections.Counter()
    expensive = []
    for a in actions:
        controller.SetFrameEvent(a.eventId, False)
        state = controller.GetPipelineState()
        is_dispatch = bool(a.flags & rd.ActionFlags.Dispatch)
        stage = rd.ShaderStage.Compute if is_dispatch else rd.ShaderStage.Pixel
        try:
            ros = state.GetReadOnlyResources(stage, True)
        except TypeError:
            ros = state.GetReadOnlyResources(stage)
        reads_rt = any(ud.descriptor.resource in host_rts for ud in ros)
        if is_dispatch:
            group = "dispatch: EDRAM dump (reads a host RT)" if reads_rt else "dispatch: resolve / texture load"
        else:
            group = "draw: ownership transfer (reads a host RT)" if reads_rt else "draw: game"
        d = durations.get(a.eventId, 0.0)
        groups[group] += d
        counts[group] += 1
        if reads_rt:
            expensive.append((d, a.eventId, group))
    total = sum(groups.values())
    log("total %.2f ms" % (total * 1000))
    for g, d in groups.most_common():
        log("%8.2f ms  %5.1f%%  %5d actions  %s" % (d * 1000, 100 * d / total, counts[g], g))
    log("--- most expensive emulation actions")
    for d, eid, g in sorted(expensive, reverse=True)[:20]:
        log("%8.3f ms  e%-6d %s" % (d * 1000, eid, g))


run(main)
