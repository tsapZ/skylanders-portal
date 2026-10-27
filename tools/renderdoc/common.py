# Shared helpers for RenderDoc scripts run through qrenderdoc --python.
# Arguments come in through environment variables (RD_CAP, RD_OUT, RD_ARGS), because qrenderdoc
# owns the command line.
import os
import sys
import traceback

import renderdoc as rd

OUT = os.environ.get("RD_OUT", "rd_out.txt")
_log = open(OUT, "w", encoding="utf-8")


def log(*parts):
    _log.write(" ".join(str(p) for p in parts) + "\n")
    _log.flush()


def open_capture():
    path = os.environ["RD_CAP"]
    cap = rd.OpenCaptureFile()
    result = cap.OpenFile(path, "", None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError("open failed: %s" % str(result))
    if not cap.LocalReplaySupport():
        raise RuntimeError("no local replay support")
    result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if result != rd.ResultCode.Succeeded:
        raise RuntimeError("replay failed: %s" % str(result))
    return cap, controller


def run(main):
    try:
        cap, controller = open_capture()
        try:
            main(controller)
        finally:
            controller.Shutdown()
            cap.Shutdown()
    except Exception:
        log("EXCEPTION")
        log(traceback.format_exc())
    finally:
        _log.close()
        os._exit(0)
