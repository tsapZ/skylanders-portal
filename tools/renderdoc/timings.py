# GPU time per action (EventGPUDuration counter), grouped by kind and by bound targets, plus the
# most expensive single actions. Shows where a frame's GPU time goes.
import collections
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    sdfile = controller.GetStructuredFile()
    textures = {t.resourceId: t for t in controller.GetTextures()}
    actions = {}

    def visit(items):
        for a in items:
            actions[a.eventId] = a
            if a.children:
                visit(a.children)

    visit(controller.GetRootActions())
    results = controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    durations = {r.eventId: r.value.d for r in results}

    def target_name(a):
        outs = [o for o in a.outputs if o != rd.ResourceId.Null()]
        parts = []
        for o in outs[:3]:
            t = textures.get(o)
            parts.append("%dx%d %s" % (t.width, t.height, t.format.Name()) if t else str(int(o)))
        if a.depthOut != rd.ResourceId.Null():
            t = textures.get(a.depthOut)
            parts.append("depth %dx%d %s" % (t.width, t.height, t.format.Name()) if t else "depth")
        return ", ".join(parts) or "-"

    total = sum(durations.values())
    by_kind = collections.Counter()
    by_target = collections.Counter()
    for eid, d in durations.items():
        a = actions.get(eid)
        if not a:
            continue
        kind = a.GetName(sdfile).split("::")[-1].split("(")[0]
        by_kind[kind] += d
        by_target[target_name(a)] += d
    log("total GPU time %.2f ms over %d actions" % (total * 1000, len(durations)))
    log("--- by kind")
    for k, d in by_kind.most_common():
        log("%8.2f ms  %5.1f%%  %s" % (d * 1000, 100 * d / total, k))
    log("--- by bound targets (top 25)")
    for k, d in by_target.most_common(25):
        log("%8.2f ms  %5.1f%%  %s" % (d * 1000, 100 * d / total, k))
    log("--- most expensive actions (top 40)")
    for eid, d in sorted(durations.items(), key=lambda x: -x[1])[:40]:
        a = actions.get(eid)
        log("%8.3f ms  e%-6d %s  [%s]" % (d * 1000, eid, a.GetName(sdfile).split("::")[-1] if a else "?",
                                          target_name(a) if a else ""))


run(main)
