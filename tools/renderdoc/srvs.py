# For every draw: the pixel shader's bound textures (size and format), one line per draw.
import os
import sys

sys.path.insert(0, os.environ['RD_DIR'])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    sdfile = controller.GetStructuredFile()
    textures = {t.resourceId: t for t in controller.GetTextures()}
    draws = []

    def visit(actions):
        for a in actions:
            if a.flags & rd.ActionFlags.Drawcall:
                draws.append(a)
            if a.children:
                visit(a.children)

    visit(controller.GetRootActions())
    for a in draws:
        controller.SetFrameEvent(a.eventId, False)
        state = controller.GetPipelineState()
        try:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
        except TypeError:
            ros = state.GetReadOnlyResources(rd.ShaderStage.Pixel)
        parts = []
        for ud in ros:
            t = textures.get(ud.descriptor.resource)
            if t:
                parts.append("%d:%dx%d:%s" % (int(ud.descriptor.resource), t.width, t.height,
                                               t.format.Name()))
        log(a.eventId, a.GetName(sdfile).split("::")[-1], " ".join(parts))


run(main)
