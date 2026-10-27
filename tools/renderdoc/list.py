# List every action of the capture: event id, marker depth, name, bound outputs.
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    sdfile = controller.GetStructuredFile()
    textures = {t.resourceId: t for t in controller.GetTextures()}
    names = {r.resourceId: r.name for r in controller.GetResources()}

    def tex_desc(rid):
        if rid == rd.ResourceId.Null():
            return "-"
        t = textures.get(rid)
        if not t:
            return str(int(rid))
        return "%s:%dx%d:%s%s" % (int(rid), t.width, t.height, t.format.Name(),
                                  (":ms%d" % t.msSamp) if t.msSamp > 1 else "")

    def visit(actions, depth):
        for a in actions:
            name = a.GetName(sdfile)
            outs = [tex_desc(o) for o in a.outputs if o != rd.ResourceId.Null()]
            depth_out = tex_desc(a.depthOut) if a.depthOut != rd.ResourceId.Null() else ""
            log("%6d %s%s%s%s" % (a.eventId, "  " * depth, name,
                                  ("  out=" + ",".join(outs)) if outs else "",
                                  ("  depth=" + depth_out) if depth_out else ""))
            if a.children:
                visit(a.children, depth + 1)

    visit(controller.GetRootActions(), 0)


run(main)
