# disasm.py (just rd disasm <capture> <out> "<eid>,<eid>"): for each listed event, the pixel
# and vertex shaders' disassembly and the pixel shader's constant buffers.
# RD_ARGS: "<eid>[,<eid>...]"
import os
import sys

sys.path.insert(0, os.environ["RD_DIR"])
from common import log, run  # noqa: E402

import renderdoc as rd  # noqa: E402


def main(controller):
    eids = [int(e) for e in os.environ.get("RD_ARGS", "").split()[0].split(",")]
    targets = controller.GetDisassemblyTargets(True)
    log("targets:", ", ".join(targets))
    for eid in eids:
        controller.SetFrameEvent(eid, True)
        state = controller.GetPipelineState()
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
            refl = state.GetShaderReflection(stage)
            if not refl:
                continue
            pipe = state.GetGraphicsPipelineObject()
            text = controller.DisassembleShader(pipe, refl, targets[0])
            log("=== event", eid, str(stage))
            log(text)
            if stage == rd.ShaderStage.Pixel:
                for i, cb in enumerate(refl.constantBlocks):
                    bind = state.GetConstantBlock(stage, i, 0)
                    vars_ = controller.GetCBufferVariableContents(pipe, refl.resourceId, stage,
                                                                 refl.entryPoint, i, bind.descriptor.resource,
                                                                 bind.descriptor.byteOffset, bind.descriptor.byteSize)
                    log("--- cbuffer", i, cb.name)
                    for v in vars_:
                        if v.members:
                            for m in v.members:
                                log("   ", m.name, [round(m.value.f32v[k], 6) for k in range(min(4, m.columns or 4))])
                        else:
                            log("   ", v.name, [round(v.value.f32v[k], 6) for k in range(min(4, v.columns or 4))])


run(main)
